#![no_std]

extern crate alloc;

use afos_api::{AppRuntime, Error, Result, SystemApi};
use alloc::{boxed::Box, string::String, string::ToString};
use core::cell::UnsafeCell;
use rhai::{Array, Dynamic, Engine, EvalAltResult, INT, ImmutableString, Map, Position};

const MAX_OPERATIONS: u64 = 5_000_000;
const MAX_CALL_LEVELS: usize = 32;
const MAX_EXPR_DEPTH: usize = 64;
const MAX_STRING_SIZE: usize = 1024 * 1024;
const MAX_COLLECTION_SIZE: usize = 16 * 1024;

type RhaiResult<T> = core::result::Result<T, Box<EvalAltResult>>;

/// Rhai requires registered callbacks to be `'static`. AFOS execution is strictly
/// single-threaded, non-reentrant, and this slot is populated only for the duration
/// of one `Engine::eval` call. `ApiGuard` clears it before the borrowed API expires.
struct ApiSlot(UnsafeCell<Option<*mut (dyn SystemApi + 'static)>>);

// SAFETY: AFOS runs application engines on one cooperative execution thread. The
// slot is guarded against nested execution and is never accessed concurrently.
unsafe impl Sync for ApiSlot {}

static API_SLOT: ApiSlot = ApiSlot(UnsafeCell::new(None));

struct ApiGuard;

impl ApiGuard {
    #[allow(clippy::transmute_ptr_to_ptr)]
    fn install(api: &mut dyn SystemApi) -> Result<Self> {
        // SAFETY: synchronized by AFOS's single-threaded runtime contract.
        let slot = unsafe { &mut *API_SLOT.0.get() };
        if slot.is_some() {
            return Err(Error::Runtime(String::from(
                "nested Rhai execution is not supported",
            )));
        }
        let scoped = core::ptr::from_mut::<dyn SystemApi + '_>(api);
        // SAFETY: `ApiGuard` clears the pointer before `api` can leave scope, and
        // all access occurs synchronously during this execution.
        let erased = unsafe {
            core::mem::transmute::<*mut (dyn SystemApi + '_), *mut (dyn SystemApi + 'static)>(
                scoped,
            )
        };
        *slot = Some(erased);
        Ok(Self)
    }
}

impl Drop for ApiGuard {
    fn drop(&mut self) {
        // SAFETY: synchronized by AFOS's single-threaded runtime contract.
        unsafe {
            *API_SLOT.0.get() = None;
        }
    }
}

fn with_api<T>(operation: impl FnOnce(&mut dyn SystemApi) -> Result<T>) -> Result<T> {
    // SAFETY: the pointer is installed by `ApiGuard` for the synchronous eval and
    // callback invocations do not overlap.
    let pointer = unsafe {
        (*API_SLOT.0.get())
            .ok_or_else(|| Error::Runtime(String::from("missing AFOS API context")))?
    };
    // SAFETY: `ApiGuard` guarantees the pointee remains valid during this call.
    operation(unsafe { &mut *pointer })
}

#[allow(clippy::needless_pass_by_value, clippy::unnecessary_box_returns)]
fn rhai_error(error: Error) -> Box<EvalAltResult> {
    EvalAltResult::ErrorRuntime(error.to_string().into(), Position::NONE).into()
}

#[derive(Default)]
pub struct RhaiRuntime;

impl RhaiRuntime {
    #[must_use]
    pub const fn new() -> Self {
        Self
    }

    #[allow(clippy::too_many_lines)]
    fn engine() -> Engine {
        let mut engine = Engine::new();
        engine
            .set_max_operations(MAX_OPERATIONS)
            .set_max_call_levels(MAX_CALL_LEVELS)
            .set_max_expr_depths(MAX_EXPR_DEPTH, MAX_EXPR_DEPTH)
            .set_max_string_size(MAX_STRING_SIZE)
            .set_max_array_size(MAX_COLLECTION_SIZE)
            .set_max_map_size(MAX_COLLECTION_SIZE);

        engine.on_print(|text| {
            let _ = with_api(|api| {
                api.console_write(text)?;
                api.console_write("\n")
            });
        });
        engine.on_debug(|text, source, position| {
            let source = source.unwrap_or("<script>");
            let _ = with_api(|api| {
                api.console_write(&alloc::format!("[debug {source}:{position}] {text}\n"))
            });
        });
        engine.on_progress(|_| {
            with_api(|api| Ok(api.cancelled()))
                .ok()
                .filter(|cancelled| *cancelled)
                .map(|_| Dynamic::from("execution cancelled"))
        });

        engine.register_fn("print_raw", |text: ImmutableString| -> RhaiResult<()> {
            with_api(|api| api.console_write(text.as_str())).map_err(rhai_error)
        });
        engine.register_fn(
            "read_line",
            |prompt: ImmutableString| -> RhaiResult<ImmutableString> {
                with_api(|api| api.console_read_line(prompt.as_str()))
                    .map(Into::into)
                    .map_err(rhai_error)
            },
        );
        engine.register_fn("args", || -> RhaiResult<Array> {
            with_api(|api| Ok(api.args().iter().cloned().map(Dynamic::from).collect()))
                .map_err(rhai_error)
        });
        engine.register_fn("cwd", || -> RhaiResult<ImmutableString> {
            with_api(|api| Ok(ImmutableString::from(api.cwd()))).map_err(rhai_error)
        });
        engine.register_fn(
            "fs_read",
            |path: ImmutableString| -> RhaiResult<ImmutableString> {
                with_api(|api| api.read_text(path.as_str()))
                    .map(Into::into)
                    .map_err(rhai_error)
            },
        );
        engine.register_fn(
            "fs_write",
            |path: ImmutableString, text: ImmutableString| -> RhaiResult<()> {
                with_api(|api| api.write_text(path.as_str(), text.as_str())).map_err(rhai_error)
            },
        );
        engine.register_fn("fs_list", |path: ImmutableString| -> RhaiResult<Array> {
            with_api(|api| {
                api.list(path.as_str()).map(|entries| {
                    entries
                        .into_iter()
                        .map(|entry| {
                            let name = if entry.is_dir {
                                alloc::format!("{}/", entry.name)
                            } else {
                                entry.name
                            };
                            Dynamic::from(name)
                        })
                        .collect()
                })
            })
            .map_err(rhai_error)
        });
        engine.register_fn("fs_mkdir", |path: ImmutableString| -> RhaiResult<()> {
            with_api(|api| api.create_dir_all(path.as_str())).map_err(rhai_error)
        });
        engine.register_fn("fs_remove", |path: ImmutableString| -> RhaiResult<()> {
            with_api(|api| api.remove(path.as_str())).map_err(rhai_error)
        });
        engine.register_fn(
            "fs_rename",
            |from: ImmutableString, to: ImmutableString| -> RhaiResult<()> {
                with_api(|api| api.rename(from.as_str(), to.as_str())).map_err(rhai_error)
            },
        );
        engine.register_fn(
            "appdata_read",
            |path: ImmutableString| -> RhaiResult<ImmutableString> {
                with_api(|api| api.appdata_read(path.as_str()))
                    .map(Into::into)
                    .map_err(rhai_error)
            },
        );
        engine.register_fn(
            "appdata_write",
            |path: ImmutableString, text: ImmutableString| -> RhaiResult<()> {
                with_api(|api| api.appdata_write(path.as_str(), text.as_str())).map_err(rhai_error)
            },
        );
        engine.register_fn("monotonic_millis", || -> RhaiResult<INT> {
            with_api(|api| api.monotonic_millis())
                .and_then(|value| {
                    i64::try_from(value).map_err(|_| {
                        Error::ResourceLimit(String::from("clock value exceeds Rhai integer"))
                    })
                })
                .map_err(rhai_error)
        });
        engine.register_fn("system_info", || -> RhaiResult<Map> {
            with_api(|api| {
                api.system_info().map(|info| {
                    let mut map = Map::new();
                    map.insert("name".into(), info.name.into());
                    map.insert("version".into(), info.version.into());
                    map.insert("platform".into(), info.platform.into());
                    map.insert("architecture".into(), info.architecture.into());
                    map
                })
            })
            .map_err(rhai_error)
        });

        engine
    }
}

impl AppRuntime for RhaiRuntime {
    fn id(&self) -> &'static str {
        "rhai"
    }

    fn extension(&self) -> &'static str {
        "rhai"
    }

    fn execute(&self, source: &str, api: &mut dyn SystemApi) -> Result<i32> {
        let _guard = ApiGuard::install(api)?;
        let result = Self::engine()
            .eval::<Dynamic>(source)
            .map_err(|error| Error::Runtime(error.to_string()))?;
        if result.is_unit() {
            return Ok(0);
        }
        result
            .try_cast::<INT>()
            .and_then(|value| i32::try_from(value).ok())
            .ok_or_else(|| {
                Error::Runtime(String::from(
                    "application must return an integer exit status",
                ))
            })
    }
}
