' Example FreeBASIC Program for AFOS
'
' This is an example of how a FreeBASIC program might look.
' Note: FreeBASIC would need a custom backend to output AFOS format.
'
' For now, compilation must be done on the host system with a custom
' FreeBASIC backend or conversion tool.

Print "Hello from FreeBASIC on AFOS!"
Print "Arguments:"
For i As Integer = 0 To CommandCount()
    Print "  " & Command(i)
Next

End 0

