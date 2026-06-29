//! Cooperative TCP/IP networking for the bare-metal kernel.
//!
//! A discovered `VirtIO` network device is wrapped as a `smoltcp` [`Device`] and
//! driven by a single-threaded poll loop. `DHCPv4` acquires an address lease, an
//! optional DNS socket resolves host names, and TCP client sockets are exposed
//! through opaque handles that the platform maps onto the AFOS `SystemApi`.

use afos_api::NetStatus;
use alloc::{
    boxed::Box,
    collections::BTreeMap,
    format,
    string::{String, ToString},
    vec,
    vec::Vec,
};
use smoltcp::{
    iface::{Config, Interface, SocketHandle, SocketSet},
    phy::{Device, DeviceCapabilities, Medium, RxToken, TxToken},
    socket::{dhcpv4, dns, tcp},
    time::Instant,
    wire::{
        DnsQueryType, EthernetAddress, HardwareAddress, IpAddress, IpCidr, Ipv4Address, Ipv4Cidr,
    },
};

/// A raw network interface card, implemented by the `VirtIO` driver.
pub trait NetDevice {
    fn mac(&self) -> [u8; 6];
    fn can_send(&self) -> bool;
    fn receive(&mut self) -> Option<Vec<u8>>;
    fn transmit(&mut self, frame: &[u8]);
}

const MTU: usize = 1514;
const TCP_RX_BUFFER: usize = 16 * 1024;
const TCP_TX_BUFFER: usize = 8 * 1024;
const FIRST_EPHEMERAL_PORT: u16 = 49152;

/// A recoverable networking failure, mapped to `afos_api::Error` by the platform.
pub enum NetError {
    NoLease,
    NameResolution(String),
    Timeout(String),
    Cancelled,
    Closed,
    Socket(String),
}

struct SmolDevice {
    nic: Box<dyn NetDevice>,
}

impl Device for SmolDevice {
    type RxToken<'a>
        = SmolRxToken
    where
        Self: 'a;
    type TxToken<'a>
        = SmolTxToken<'a>
    where
        Self: 'a;

    fn receive(&mut self, _timestamp: Instant) -> Option<(Self::RxToken<'_>, Self::TxToken<'_>)> {
        let frame = self.nic.receive()?;
        Some((
            SmolRxToken { frame },
            SmolTxToken {
                nic: self.nic.as_mut(),
            },
        ))
    }

    fn transmit(&mut self, _timestamp: Instant) -> Option<Self::TxToken<'_>> {
        if self.nic.can_send() {
            Some(SmolTxToken {
                nic: self.nic.as_mut(),
            })
        } else {
            None
        }
    }

    fn capabilities(&self) -> DeviceCapabilities {
        let mut capabilities = DeviceCapabilities::default();
        capabilities.medium = Medium::Ethernet;
        capabilities.max_transmission_unit = MTU;
        capabilities
    }
}

struct SmolRxToken {
    frame: Vec<u8>,
}

impl RxToken for SmolRxToken {
    fn consume<R, F: FnOnce(&[u8]) -> R>(self, f: F) -> R {
        f(&self.frame)
    }
}

struct SmolTxToken<'a> {
    nic: &'a mut dyn NetDevice,
}

impl TxToken for SmolTxToken<'_> {
    fn consume<R, F: FnOnce(&mut [u8]) -> R>(self, len: usize, f: F) -> R {
        let mut buffer = vec![0_u8; len];
        let result = f(&mut buffer);
        self.nic.transmit(&buffer);
        result
    }
}

pub struct NetStack {
    device: SmolDevice,
    iface: Interface,
    sockets: SocketSet<'static>,
    dhcp: SocketHandle,
    dns: SocketHandle,
    connections: BTreeMap<u64, SocketHandle>,
    next_handle: u64,
    next_port: u16,
    mac: [u8; 6],
    address: Option<Ipv4Cidr>,
    gateway: Option<Ipv4Address>,
    dns_servers: Vec<IpAddress>,
}

impl NetStack {
    pub fn new(nic: Box<dyn NetDevice>, now_millis: u64, seed: u64) -> Self {
        let mac = nic.mac();
        let mut device = SmolDevice { nic };
        let mut config = Config::new(HardwareAddress::Ethernet(EthernetAddress(mac)));
        config.random_seed = seed;
        let iface = Interface::new(
            config,
            &mut device,
            Instant::from_millis(i64::try_from(now_millis).unwrap_or(i64::MAX)),
        );

        let mut sockets = SocketSet::new(Vec::new());
        let dhcp = sockets.add(dhcpv4::Socket::new());
        let queries: Vec<Option<dns::DnsQuery>> = (0..1).map(|_| None).collect();
        let dns = sockets.add(dns::Socket::new(&[], queries));

        Self {
            device,
            iface,
            sockets,
            dhcp,
            dns,
            connections: BTreeMap::new(),
            next_handle: 1,
            next_port: FIRST_EPHEMERAL_PORT,
            mac,
            address: None,
            gateway: None,
            dns_servers: Vec::new(),
        }
    }

    /// Advances the stack and applies any DHCP lease change.
    pub fn poll(&mut self, now_millis: u64) {
        let now = Instant::from_millis(i64::try_from(now_millis).unwrap_or(i64::MAX));
        self.iface.poll(now, &mut self.device, &mut self.sockets);

        let lease = {
            let socket = self.sockets.get_mut::<dhcpv4::Socket>(self.dhcp);
            match socket.poll() {
                Some(dhcpv4::Event::Configured(config)) => {
                    let servers: Vec<IpAddress> = config
                        .dns_servers
                        .iter()
                        .map(|a| IpAddress::Ipv4(*a))
                        .collect();
                    Some(Some((config.address, config.router, servers)))
                }
                Some(dhcpv4::Event::Deconfigured) => Some(None),
                None => None,
            }
        };

        match lease {
            Some(Some((address, router, servers))) => {
                self.iface.update_ip_addrs(|addresses| {
                    addresses.clear();
                    let _ = addresses.push(IpCidr::Ipv4(address));
                });
                self.iface.routes_mut().remove_default_ipv4_route();
                if let Some(gateway) = router {
                    let _ = self.iface.routes_mut().add_default_ipv4_route(gateway);
                }
                self.sockets
                    .get_mut::<dns::Socket>(self.dns)
                    .update_servers(&servers);
                self.address = Some(address);
                self.gateway = router;
                self.dns_servers = servers;
            }
            Some(None) => {
                self.iface.update_ip_addrs(|addresses| {
                    addresses.clear();
                });
                self.iface.routes_mut().remove_default_ipv4_route();
                self.address = None;
                self.gateway = None;
            }
            None => {}
        }
    }

    #[must_use]
    pub fn is_configured(&self) -> bool {
        self.address.is_some()
    }

    #[must_use]
    pub fn status(&self) -> NetStatus {
        NetStatus {
            link_up: true,
            mac: format_mac(self.mac),
            address: self
                .address
                .map(|cidr| format!("{}/{}", cidr.address(), cidr.prefix_len())),
            gateway: self.gateway.map(|address| address.to_string()),
        }
    }

    fn allocate_port(&mut self) -> u16 {
        let port = self.next_port;
        self.next_port = if self.next_port == u16::MAX {
            FIRST_EPHEMERAL_PORT
        } else {
            self.next_port + 1
        };
        port
    }

    /// Resolves `host` to an IPv4 address, polling until the query completes.
    fn resolve(
        &mut self,
        host: &str,
        clock: &mut dyn FnMut() -> u64,
        cancel: &mut dyn FnMut() -> bool,
        deadline: u64,
    ) -> Result<Ipv4Address, NetError> {
        if let Some(address) = parse_ipv4(host) {
            return Ok(address);
        }
        if self.dns_servers.is_empty() {
            return Err(NetError::NameResolution(String::from(
                "no DNS server was offered by DHCP",
            )));
        }

        let query = {
            let context = self.iface.context();
            self.sockets
                .get_mut::<dns::Socket>(self.dns)
                .start_query(context, host, DnsQueryType::A)
                .map_err(|error| NetError::NameResolution(format!("{error:?}")))?
        };

        loop {
            self.poll(clock());
            match self
                .sockets
                .get_mut::<dns::Socket>(self.dns)
                .get_query_result(query)
            {
                Ok(addresses) => {
                    return addresses
                        .first()
                        .map(|address| match address {
                            IpAddress::Ipv4(value) => *value,
                        })
                        .ok_or_else(|| {
                            NetError::NameResolution(String::from("host has no IPv4 address"))
                        });
                }
                Err(dns::GetQueryResultError::Pending) => {}
                Err(error) => {
                    return Err(NetError::NameResolution(format!("{error:?}")));
                }
            }
            if cancel() {
                return Err(NetError::Cancelled);
            }
            if clock() >= deadline {
                return Err(NetError::Timeout(String::from("DNS resolution timed out")));
            }
        }
    }

    /// Opens a TCP connection, returning an AFOS connection handle.
    pub fn connect(
        &mut self,
        host: &str,
        port: u16,
        timeout_millis: u64,
        clock: &mut dyn FnMut() -> u64,
        cancel: &mut dyn FnMut() -> bool,
    ) -> Result<u64, NetError> {
        let deadline = clock().saturating_add(timeout_millis);

        while !self.is_configured() {
            self.poll(clock());
            if self.is_configured() {
                break;
            }
            if cancel() {
                return Err(NetError::Cancelled);
            }
            if clock() >= deadline {
                return Err(NetError::NoLease);
            }
        }

        let address = self.resolve(host, clock, cancel, deadline)?;
        let local_port = self.allocate_port();
        let socket = tcp::Socket::new(
            tcp::SocketBuffer::new(vec![0_u8; TCP_RX_BUFFER]),
            tcp::SocketBuffer::new(vec![0_u8; TCP_TX_BUFFER]),
        );
        let handle = self.sockets.add(socket);
        {
            let context = self.iface.context();
            self.sockets
                .get_mut::<tcp::Socket>(handle)
                .connect(context, (IpAddress::Ipv4(address), port), local_port)
                .map_err(|error| NetError::Socket(format!("{error:?}")))?;
        }

        loop {
            self.poll(clock());
            let state = self.sockets.get_mut::<tcp::Socket>(handle).state();
            match state {
                tcp::State::Established => break,
                tcp::State::Closed => {
                    self.sockets.remove(handle);
                    return Err(NetError::Socket(String::from("connection refused")));
                }
                _ => {}
            }
            if cancel() {
                self.sockets.remove(handle);
                return Err(NetError::Cancelled);
            }
            if clock() >= deadline {
                self.sockets.remove(handle);
                return Err(NetError::Timeout(String::from("connection timed out")));
            }
        }

        let id = self.next_handle;
        self.next_handle += 1;
        self.connections.insert(id, handle);
        Ok(id)
    }

    pub fn send(
        &mut self,
        id: u64,
        data: &[u8],
        clock: &mut dyn FnMut() -> u64,
        cancel: &mut dyn FnMut() -> bool,
    ) -> Result<usize, NetError> {
        let handle = *self.connections.get(&id).ok_or(NetError::Closed)?;
        let deadline = clock().saturating_add(15_000);
        loop {
            self.poll(clock());
            let socket = self.sockets.get_mut::<tcp::Socket>(handle);
            if !socket.may_send() {
                return Err(NetError::Closed);
            }
            if socket.can_send() {
                let written = socket
                    .send_slice(data)
                    .map_err(|error| NetError::Socket(format!("{error:?}")))?;
                self.poll(clock());
                return Ok(written);
            }
            if cancel() {
                return Err(NetError::Cancelled);
            }
            if clock() >= deadline {
                return Err(NetError::Timeout(String::from("send timed out")));
            }
        }
    }

    pub fn recv(
        &mut self,
        id: u64,
        output: &mut [u8],
        clock: &mut dyn FnMut() -> u64,
        cancel: &mut dyn FnMut() -> bool,
    ) -> Result<usize, NetError> {
        let handle = *self.connections.get(&id).ok_or(NetError::Closed)?;
        let deadline = clock().saturating_add(15_000);
        loop {
            self.poll(clock());
            let socket = self.sockets.get_mut::<tcp::Socket>(handle);
            if socket.can_recv() {
                return socket
                    .recv_slice(output)
                    .map_err(|error| NetError::Socket(format!("{error:?}")));
            }
            if !socket.may_recv() {
                // The peer closed its half of the connection and drained data.
                return Ok(0);
            }
            if cancel() {
                return Err(NetError::Cancelled);
            }
            if clock() >= deadline {
                return Err(NetError::Timeout(String::from("receive timed out")));
            }
        }
    }

    pub fn close(&mut self, id: u64, clock: &mut dyn FnMut() -> u64) {
        if let Some(handle) = self.connections.remove(&id) {
            self.sockets.get_mut::<tcp::Socket>(handle).close();
            self.poll(clock());
            self.sockets.remove(handle);
        }
    }
}

fn parse_ipv4(value: &str) -> Option<Ipv4Address> {
    let mut octets = [0_u8; 4];
    let mut parts = value.split('.');
    for octet in &mut octets {
        *octet = parts.next()?.parse().ok()?;
    }
    if parts.next().is_some() {
        return None;
    }
    Some(Ipv4Address::from(octets))
}

fn format_mac(mac: [u8; 6]) -> String {
    use core::fmt::Write;
    let mut output = String::new();
    for (index, byte) in mac.iter().enumerate() {
        if index != 0 {
            output.push(':');
        }
        let _ = write!(output, "{byte:02x}");
    }
    output
}
