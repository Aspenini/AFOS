' AFOS Calculator - CLI Mode
' Simple command-line calculator

PRINT "AFOS Calculator (CLI)"
PRINT "Operations: 1=+, 2=-, 3=*, 4=/"
PRINT "Enter 0 to exit"
PRINT ""

LET running = 1

WHILE running = 1
    PRINT "calc> "
    PRINT "First number (0 to exit): "
    INPUT num1
    
    IF num1 = 0 THEN
        LET running = 0
        PRINT "Goodbye!"
    ELSE
        PRINT "Operation (1=+, 2=-, 3=*, 4=/): "
        INPUT op
        PRINT "Second number: "
        INPUT num2
        
        IF op = 1 THEN
            LET result = num1 + num2
            PRINT num1, " + ", num2, " = ", result
        ELSE
            IF op = 2 THEN
                LET result = num1 - num2
                PRINT num1, " - ", num2, " = ", result
            ELSE
                IF op = 3 THEN
                    LET result = num1 * num2
                    PRINT num1, " * ", num2, " = ", result
                ELSE
                    IF op = 4 THEN
                        IF num2 = 0 THEN
                            PRINT "Error: Division by zero!"
                        ELSE
                            LET result = num1 / num2
                            PRINT num1, " / ", num2, " = ", result
                        ENDIF
                    ELSE
                        PRINT "Invalid operation! Use 1, 2, 3, or 4"
                    ENDIF
                ENDIF
            ENDIF
        ENDIF
    ENDIF
    PRINT ""
WEND

