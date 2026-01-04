#include "basic.h"
#include "filesystem.h"
#include "keyboard.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* str);
void terminal_writestring_color(const char* data, uint8_t color);
void terminal_putchar(char c);
int keyboard_getchar(void);
void* malloc(uint32_t size);
void free(void* ptr);
void malloc_reset(void);

// Color constants (VGA colors)
#define COLOR_RED 0x0C  // Light red on black

// Token types
typedef enum {
    TOK_EOF,
    TOK_NUMBER,
    TOK_STRING,
    TOK_IDENTIFIER,
    TOK_NEWLINE,
    TOK_COMMA,
    TOK_SEMICOLON,
    TOK_COLON,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_EQUAL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MULTIPLY,
    TOK_DIVIDE,
    TOK_MOD,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,
    TOK_EQ,
    TOK_NE,
    TOK_AND,
    TOK_OR,
    TOK_NOT
} basic_token_type_t;

// Value types
typedef enum {
    VAL_NUMBER,
    VAL_STRING
} basic_value_type_t;

// Value union
typedef union {
    double number;
    char* string;
} basic_value_t;

// Variable
typedef struct basic_var {
    char name[64];
    basic_value_type_t type;
    basic_value_t value;
    struct basic_var* next;
} basic_var_t;

// Tokenizer state
static const char* basic_source = NULL;
static const char* basic_pos = NULL;
static basic_token_type_t basic_current_token = TOK_EOF;
static double basic_token_number = 0.0;
static char basic_token_string[256] = {0};
static char basic_token_ident[64] = {0};

// Variable storage (hash table)
#define VAR_TABLE_SIZE 256
static basic_var_t* var_table[VAR_TABLE_SIZE];

// Simple hash function
static uint32_t basic_hash(const char* name) {
    uint32_t hash = 0;
    while (*name) {
        hash = hash * 31 + *name++;
    }
    return hash % VAR_TABLE_SIZE;
}

// Get variable
static basic_var_t* basic_get_var(const char* name) {
    uint32_t idx = basic_hash(name);
    basic_var_t* var = var_table[idx];
    while (var) {
        uint32_t i = 0;
        int match = 1;
        while (name[i] != '\0' && var->name[i] != '\0') {
            if (name[i] != var->name[i]) {
                match = 0;
                break;
            }
            i++;
        }
        if (match && name[i] == '\0' && var->name[i] == '\0') {
            return var;
        }
        var = var->next;
    }
    return NULL;
}

// Set variable
static basic_var_t* basic_set_var(const char* name, basic_value_type_t type, basic_value_t value) {
    basic_var_t* var = basic_get_var(name);
    if (!var) {
        var = (basic_var_t*)malloc(sizeof(basic_var_t));
        if (!var) return NULL;
        uint32_t i = 0;
        while (name[i] != '\0' && i < 63) {
            var->name[i] = name[i];
            i++;
        }
        var->name[i] = '\0';
        uint32_t idx = basic_hash(name);
        var->next = var_table[idx];
        var_table[idx] = var;
    }
    var->type = type;
    if (type == VAL_STRING && value.string) {
        uint32_t len = 0;
        while (value.string[len] != '\0') len++;
        var->value.string = (char*)malloc(len + 1);
        if (var->value.string) {
            for (uint32_t i = 0; i <= len; i++) {
                var->value.string[i] = value.string[i];
            }
        }
    } else {
        var->value = value;
    }
    return var;
}

// Skip whitespace
static void basic_skip_whitespace(void) {
    while (*basic_pos == ' ' || *basic_pos == '\t') {
        basic_pos++;
    }
}

// Tokenize number
static int basic_tokenize_number(void) {
    double num = 0.0;
    double fraction = 0.1;
    int has_dot = 0;
    int negative = 0;
    
    if (*basic_pos == '-') {
        negative = 1;
        basic_pos++;
    } else if (*basic_pos == '+') {
        basic_pos++;
    }
    
    while ((*basic_pos >= '0' && *basic_pos <= '9') || *basic_pos == '.') {
        if (*basic_pos == '.') {
            if (has_dot) break;
            has_dot = 1;
            basic_pos++;
            continue;
        }
        if (has_dot) {
            num += (*basic_pos - '0') * fraction;
            fraction *= 0.1;
        } else {
            num = num * 10 + (*basic_pos - '0');
        }
        basic_pos++;
    }
    basic_token_number = negative ? -num : num;
    return 1;
}

// Tokenize string
static int basic_tokenize_string(void) {
    basic_pos++; // Skip opening quote
    uint32_t i = 0;
    while (*basic_pos != '"' && *basic_pos != '\0' && i < 255) {
        if (*basic_pos == '\\') {
            basic_pos++;
            switch (*basic_pos) {
                case 'n': basic_token_string[i++] = '\n'; break;
                case 't': basic_token_string[i++] = '\t'; break;
                case 'r': basic_token_string[i++] = '\r'; break;
                case '\\': basic_token_string[i++] = '\\'; break;
                case '"': basic_token_string[i++] = '"'; break;
                default: basic_token_string[i++] = *basic_pos; break;
            }
        } else {
            basic_token_string[i++] = *basic_pos;
        }
        basic_pos++;
    }
    basic_token_string[i] = '\0';
    if (*basic_pos == '"') basic_pos++;
    return 1;
}

// Tokenize identifier/keyword
static int basic_tokenize_identifier(void) {
    uint32_t i = 0;
    while ((*basic_pos >= 'a' && *basic_pos <= 'z') ||
           (*basic_pos >= 'A' && *basic_pos <= 'Z') ||
           (*basic_pos >= '0' && *basic_pos <= '9') ||
           *basic_pos == '_' || *basic_pos == '$') {
        if (i < 63) {
            basic_token_ident[i++] = *basic_pos;
        }
        basic_pos++;
    }
    basic_token_ident[i] = '\0';
    
    // Convert to uppercase for keyword matching
    for (i = 0; basic_token_ident[i]; i++) {
        if (basic_token_ident[i] >= 'a' && basic_token_ident[i] <= 'z') {
            basic_token_ident[i] = basic_token_ident[i] - 'a' + 'A';
        }
    }
    return 1;
}

// Get next token
static void basic_next_token(void) {
    basic_skip_whitespace();
    
    if (*basic_pos == '\0') {
        basic_current_token = TOK_EOF;
        return;
    }
    
    if (*basic_pos == '\n' || (*basic_pos == '\r' && basic_pos[1] == '\n')) {
        if (*basic_pos == '\r') basic_pos++;
        basic_pos++;
        basic_current_token = TOK_NEWLINE;
        return;
    }
    
    if ((*basic_pos >= '0' && *basic_pos <= '9') || 
        (*basic_pos == '-' && basic_pos[1] >= '0' && basic_pos[1] <= '9') ||
        (*basic_pos == '+' && basic_pos[1] >= '0' && basic_pos[1] <= '9')) {
        basic_tokenize_number();
        basic_current_token = TOK_NUMBER;
        return;
    }
    
    if (*basic_pos == '"') {
        basic_tokenize_string();
        basic_current_token = TOK_STRING;
        return;
    }
    
    if ((*basic_pos >= 'a' && *basic_pos <= 'z') ||
        (*basic_pos >= 'A' && *basic_pos <= 'Z') || 
        *basic_pos == '_') {
        basic_tokenize_identifier();
        basic_current_token = TOK_IDENTIFIER;
        return;
    }
    
    // Operators
    switch (*basic_pos) {
        case '+': basic_pos++; basic_current_token = TOK_PLUS; return;
        case '-': basic_pos++; basic_current_token = TOK_MINUS; return;
        case '*': basic_pos++; basic_current_token = TOK_MULTIPLY; return;
        case '/': basic_pos++; basic_current_token = TOK_DIVIDE; return;
        case '%': basic_pos++; basic_current_token = TOK_MOD; return;
        case '(': basic_pos++; basic_current_token = TOK_LPAREN; return;
        case ')': basic_pos++; basic_current_token = TOK_RPAREN; return;
        case ',': basic_pos++; basic_current_token = TOK_COMMA; return;
        case ';': basic_pos++; basic_current_token = TOK_SEMICOLON; return;
        case ':': basic_pos++; basic_current_token = TOK_COLON; return;
        case '=': 
            basic_pos++;
            if (*basic_pos == '=') {
                basic_pos++;
                basic_current_token = TOK_EQ;
            } else {
                basic_current_token = TOK_EQUAL;
            }
            return;
        case '<':
            basic_pos++;
            if (*basic_pos == '=') {
                basic_pos++;
                basic_current_token = TOK_LE;
            } else if (*basic_pos == '>') {
                basic_pos++;
                basic_current_token = TOK_NE;
            } else {
                basic_current_token = TOK_LT;
            }
            return;
        case '>':
            basic_pos++;
            if (*basic_pos == '=') {
                basic_pos++;
                basic_current_token = TOK_GE;
            } else {
                basic_current_token = TOK_GT;
            }
            return;
        case '&':
            basic_pos++;
            basic_current_token = TOK_AND;
            return;
        case '|':
            basic_pos++;
            if (*basic_pos == '|') {
                basic_pos++;
                basic_current_token = TOK_OR;
            }
            return;
        case '!':
            basic_pos++;
            if (*basic_pos == '=') {
                basic_pos++;
                basic_current_token = TOK_NE;
            } else {
                basic_current_token = TOK_NOT;
            }
            return;
        default:
            basic_pos++;
            basic_current_token = TOK_EOF;
            return;
    }
}

// String comparison helper
static int basic_strcmp(const char* s1, const char* s2) {
    uint32_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return 0;
        i++;
    }
    return (s1[i] == '\0' && s2[i] == '\0') ? 1 : 0;
}

// Expression evaluation (recursive descent)
static double basic_eval_expression(void);
static double basic_eval_logical_or(void);
static double basic_eval_logical_and(void);
static double basic_eval_comparison(void);
static double basic_eval_additive(void);
static double basic_eval_multiplicative(void);
static double basic_eval_unary(void);
static double basic_eval_factor(void);

static double basic_eval_factor(void) {
    double result = 0.0;
    
    if (basic_current_token == TOK_NUMBER) {
        result = basic_token_number;
        basic_next_token();
    } else if (basic_current_token == TOK_STRING) {
        result = 0.0;
        basic_next_token();
    } else if (basic_current_token == TOK_IDENTIFIER) {
        basic_var_t* var = basic_get_var(basic_token_ident);
        if (var && var->type == VAL_NUMBER) {
            result = var->value.number;
        } else {
            result = 0.0;
        }
        basic_next_token();
    } else if (basic_current_token == TOK_LPAREN) {
        basic_next_token();
        result = basic_eval_expression();
        if (basic_current_token == TOK_RPAREN) {
            basic_next_token();
        }
    } else if (basic_current_token == TOK_MINUS) {
        basic_next_token();
        result = -basic_eval_unary();
    } else if (basic_current_token == TOK_PLUS) {
        basic_next_token();
        result = basic_eval_unary();
    } else if (basic_current_token == TOK_NOT) {
        basic_next_token();
        result = basic_eval_unary() == 0.0 ? 1.0 : 0.0;
    }
    
    return result;
}

static double basic_eval_unary(void) {
    return basic_eval_factor();
}

static double basic_eval_multiplicative(void) {
    double result = basic_eval_unary();
    
    while (basic_current_token == TOK_MULTIPLY || 
           basic_current_token == TOK_DIVIDE ||
           basic_current_token == TOK_MOD) {
        basic_token_type_t op = basic_current_token;
        basic_next_token();
        double right = basic_eval_unary();
        
        if (op == TOK_MULTIPLY) {
            result *= right;
        } else if (op == TOK_DIVIDE) {
            if (right != 0.0) {
                result /= right;
            }
        } else if (op == TOK_MOD) {
            result = (double)((int)result % (int)right);
        }
    }
    
    return result;
}

static double basic_eval_additive(void) {
    double result = basic_eval_multiplicative();
    
    while (basic_current_token == TOK_PLUS || basic_current_token == TOK_MINUS) {
        basic_token_type_t op = basic_current_token;
        basic_next_token();
        double right = basic_eval_multiplicative();
        
        if (op == TOK_PLUS) {
            result += right;
        } else {
            result -= right;
        }
    }
    
    return result;
}

static double basic_eval_comparison(void) {
    double left = basic_eval_additive();
    
    if (basic_current_token == TOK_LT || basic_current_token == TOK_GT ||
        basic_current_token == TOK_LE || basic_current_token == TOK_GE ||
        basic_current_token == TOK_EQ || basic_current_token == TOK_NE) {
        basic_token_type_t op = basic_current_token;
        basic_next_token();
        double right = basic_eval_additive();
        
        switch (op) {
            case TOK_LT: return left < right ? 1.0 : 0.0;
            case TOK_GT: return left > right ? 1.0 : 0.0;
            case TOK_LE: return left <= right ? 1.0 : 0.0;
            case TOK_GE: return left >= right ? 1.0 : 0.0;
            case TOK_EQ: return left == right ? 1.0 : 0.0;
            case TOK_NE: return left != right ? 1.0 : 0.0;
            default: break;
        }
    }
    
    return left;
}

static double basic_eval_logical_and(void) {
    double result = basic_eval_comparison();
    
    while (basic_current_token == TOK_AND || 
           (basic_current_token == TOK_IDENTIFIER && basic_strcmp(basic_token_ident, "AND"))) {
        if (basic_current_token == TOK_IDENTIFIER) {
            basic_next_token();
        } else {
            basic_next_token();
        }
        double right = basic_eval_comparison();
        result = (result != 0.0 && right != 0.0) ? 1.0 : 0.0;
    }
    
    return result;
}

static double basic_eval_logical_or(void) {
    double result = basic_eval_logical_and();
    
    while (basic_current_token == TOK_OR ||
           (basic_current_token == TOK_IDENTIFIER && basic_strcmp(basic_token_ident, "OR"))) {
        if (basic_current_token == TOK_IDENTIFIER) {
            basic_next_token();
        } else {
            basic_next_token();
        }
        double right = basic_eval_logical_and();
        result = (result != 0.0 || right != 0.0) ? 1.0 : 0.0;
    }
    
    return result;
}

static double basic_eval_expression(void) {
    return basic_eval_logical_or();
}

// Number to string conversion
static char* basic_num_to_str(double num) {
    char* str = (char*)malloc(32);
    if (!str) return NULL;
    
    int num_int = (int)num;
    int i = 0;
    int negative = 0;
    
    if (num_int < 0) {
        negative = 1;
        num_int = -num_int;
    }
    
    if (num_int == 0) {
        str[i++] = '0';
    } else {
        char temp[32];
        int j = 0;
        while (num_int > 0) {
            temp[j++] = '0' + (num_int % 10);
            num_int /= 10;
        }
        while (j > 0) {
            str[i++] = temp[--j];
        }
    }
    str[i] = '\0';
    return str;
}

// Execute PRINT statement
static void basic_execute_print(void) {
    int first = 1;
    int no_newline = 0;
    
    while (basic_current_token != TOK_NEWLINE && 
           basic_current_token != TOK_EOF &&
           basic_current_token != TOK_COLON) {
        if (!first) {
            if (basic_current_token == TOK_COMMA) {
                basic_next_token();
                terminal_putchar(' ');
            } else if (basic_current_token == TOK_SEMICOLON) {
                basic_next_token();
                no_newline = 1;
            }
        }
        
        if (basic_current_token == TOK_STRING) {
            terminal_writestring(basic_token_string);
            basic_next_token();
        } else if (basic_current_token == TOK_NUMBER) {
            char* num_str = basic_num_to_str(basic_token_number);
            if (num_str) {
                terminal_writestring(num_str);
                free(num_str);
            }
            basic_next_token();
        } else if (basic_current_token == TOK_IDENTIFIER) {
            basic_var_t* var = basic_get_var(basic_token_ident);
            if (var) {
                if (var->type == VAL_STRING && var->value.string) {
                    terminal_writestring(var->value.string);
                } else {
                    char* num_str = basic_num_to_str(var->value.number);
                    if (num_str) {
                        terminal_writestring(num_str);
                        free(num_str);
                    }
                }
            }
            basic_next_token();
        } else {
            break;
        }
        
        first = 0;
    }
    
    if (!no_newline) {
        terminal_putchar('\n');
    }
}

// Execute LET/assignment
static void basic_execute_let(void) {
    if (basic_current_token == TOK_IDENTIFIER) {
        char var_name[64];
        uint32_t i = 0;
        while (basic_token_ident[i] != '\0' && i < 63) {
            var_name[i] = basic_token_ident[i];
            i++;
        }
        var_name[i] = '\0';
        
        // Check for string variable ($ suffix)
        int is_string = (var_name[i-1] == '$');
        if (is_string) {
            var_name[i-1] = '\0';
        }
        
        basic_next_token();
        
        if (basic_current_token == TOK_EQUAL) {
            basic_next_token();
            
            if (is_string) {
                if (basic_current_token == TOK_STRING) {
                    uint32_t len = 0;
                    while (basic_token_string[len] != '\0') len++;
                    char* str_val = (char*)malloc(len + 1);
                    if (str_val) {
                        for (uint32_t j = 0; j <= len; j++) {
                            str_val[j] = basic_token_string[j];
                        }
                    }
                    basic_value_t val;
                    val.string = str_val;
                    basic_set_var(var_name, VAL_STRING, val);
                    basic_next_token();
                } else {
                    // Convert number to string
                    double num = basic_eval_expression();
                    char* str_val = basic_num_to_str(num);
                    basic_value_t val;
                    val.string = str_val;
                    basic_set_var(var_name, VAL_STRING, val);
                }
            } else {
                double value = basic_eval_expression();
                basic_value_t val;
                val.number = value;
                basic_set_var(var_name, VAL_NUMBER, val);
            }
        }
    }
}

// Execute IF/THEN/ELSE
static int basic_execute_if(void) {
    double condition = basic_eval_expression();
    
    if (basic_current_token == TOK_IDENTIFIER && basic_strcmp(basic_token_ident, "THEN")) {
        basic_next_token();
    }
    
    if (condition != 0.0) {
        // Execute THEN block
        return 0;
    } else {
        // Skip to ELSE or ENDIF
        int depth = 1;
        while (depth > 0 && basic_current_token != TOK_EOF) {
            if (basic_current_token == TOK_IDENTIFIER) {
                if (basic_strcmp(basic_token_ident, "IF")) {
                    depth++;
                } else if (basic_strcmp(basic_token_ident, "ENDIF") || basic_strcmp(basic_token_ident, "ELSE")) {
                    if (basic_strcmp(basic_token_ident, "ELSE") && depth == 1) {
                        basic_next_token();
                        return 0; // Execute ELSE block
                    }
                    depth--;
                }
            }
            if (depth > 0) {
                while (basic_current_token != TOK_NEWLINE && 
                       basic_current_token != TOK_EOF &&
                       basic_current_token != TOK_COLON) {
                    basic_next_token();
                }
                if (basic_current_token == TOK_NEWLINE) {
                    basic_next_token();
                } else if (basic_current_token == TOK_COLON) {
                    basic_next_token();
                }
            }
        }
    }
    return 0;
}

// Execute FOR loop
static int basic_execute_for(void) {
    if (basic_current_token == TOK_IDENTIFIER) {
        char var_name[64];
        uint32_t i = 0;
        while (basic_token_ident[i] != '\0' && i < 63) {
            var_name[i] = basic_token_ident[i];
            i++;
        }
        var_name[i] = '\0';
        basic_next_token();
        
        if (basic_current_token == TOK_EQUAL) {
            basic_next_token();
            double start = basic_eval_expression();
            
            if (basic_current_token == TOK_IDENTIFIER && basic_strcmp(basic_token_ident, "TO")) {
                basic_next_token();
                double end = basic_eval_expression();
                double step = 1.0;
                
                if (basic_current_token == TOK_IDENTIFIER && basic_strcmp(basic_token_ident, "STEP")) {
                    basic_next_token();
                    step = basic_eval_expression();
                }
                
                // Save loop state (simplified - just set variable)
                basic_value_t val;
                val.number = start;
                basic_set_var(var_name, VAL_NUMBER, val);
                
                // Store loop info for NEXT (simplified implementation)
                // For now, we'll handle it in NEXT
            }
        }
    }
    return 0;
}

// Execute NEXT (simplified - doesn't fully handle nested loops)
static int basic_execute_next(void) {
    // Simplified: just continue to next iteration
    // In a full implementation, you'd track loop state
    return 0;
}

// Execute WHILE
static int basic_execute_while(void) {
    const char* while_pos = basic_pos;
    double condition = basic_eval_expression();
    
    if (condition == 0.0) {
        // Skip to WEND
        int depth = 1;
        while (depth > 0 && basic_current_token != TOK_EOF) {
            if (basic_current_token == TOK_IDENTIFIER) {
                if (basic_strcmp(basic_token_ident, "WHILE")) {
                    depth++;
                } else if (basic_strcmp(basic_token_ident, "WEND")) {
                    depth--;
                }
            }
            if (depth > 0) {
                while (basic_current_token != TOK_NEWLINE && 
                       basic_current_token != TOK_EOF &&
                       basic_current_token != TOK_COLON) {
                    basic_next_token();
                }
                if (basic_current_token == TOK_NEWLINE) {
                    basic_next_token();
                } else if (basic_current_token == TOK_COLON) {
                    basic_next_token();
                }
            }
        }
    }
    return 0;
}

// Execute INPUT
static void basic_execute_input(void) {
    if (basic_current_token == TOK_IDENTIFIER) {
        char var_name[64];
        uint32_t i = 0;
        while (basic_token_ident[i] != '\0' && i < 63) {
            var_name[i] = basic_token_ident[i];
            i++;
        }
        var_name[i] = '\0';
        
        int is_string = (var_name[i-1] == '$');
        if (is_string) {
            var_name[i-1] = '\0';
        }
        
        basic_next_token();
        
        // Simple input (read from keyboard)
        if (is_string) {
            char input[256] = {0};
            int idx = 0;
            int c;
            // Forward declaration
            extern void keyboard_handler(void);
            while (1) {
                keyboard_handler(); // Poll keyboard
                keyboard_handler();
                c = keyboard_getchar();
                if (c == '\n' || c == '\r') break;
                if (idx >= 255) break;
                if (c >= 32 && c < 127) {
                    input[idx++] = (char)c;
                    terminal_putchar((char)c);
                } else if (c == '\b' && idx > 0) {
                    idx--;
                    terminal_putchar('\b');
                } else if (c == -1) {
                    // No input available, wait a bit
                    for (volatile int i = 0; i < 1000; i++);
                }
            }
            input[idx] = '\0';
            terminal_putchar('\n');
            
            char* str_val = (char*)malloc(idx + 1);
            if (str_val) {
                for (int j = 0; j <= idx; j++) {
                    str_val[j] = input[j];
                }
            }
            basic_value_t val;
            val.string = str_val;
            basic_set_var(var_name, VAL_STRING, val);
        } else {
            // Number input - read from keyboard
            char input[256] = {0};
            int idx = 0;
            int c;
            // Forward declaration
            extern void keyboard_handler(void);
            while (1) {
                keyboard_handler(); // Poll keyboard
                keyboard_handler();
                c = keyboard_getchar();
                if (c == '\n' || c == '\r') break;
                if (idx >= 255) break;
                if (c >= 32 && c < 127) {
                    input[idx++] = (char)c;
                    terminal_putchar((char)c);
                } else if (c == '\b' && idx > 0) {
                    idx--;
                    terminal_putchar('\b');
                } else if (c == -1) {
                    // No input available, wait a bit
                    for (volatile int i = 0; i < 1000; i++);
                }
            }
            input[idx] = '\0';
            terminal_putchar('\n');
            
            // Parse number (simplified - just try to convert)
            double num = 0.0;
            // Simple number parsing
            int j = 0;
            int negative = 0;
            if (input[0] == '-') {
                negative = 1;
                j = 1;
            }
            while (input[j] >= '0' && input[j] <= '9') {
                num = num * 10.0 + (input[j] - '0');
                j++;
            }
            if (negative) num = -num;
            
            basic_value_t val;
            val.number = num;
            basic_set_var(var_name, VAL_NUMBER, val);
        }
    }
}

// Main execution loop
int basic_execute(const char* source) {
    // Initialize
    basic_source = source;
    basic_pos = source;
    
    // Clear variables
    for (int i = 0; i < VAR_TABLE_SIZE; i++) {
        var_table[i] = NULL;
    }
    
    // Reset malloc pool
    malloc_reset();
    
    basic_next_token();
    
    // Execution loop
    while (basic_current_token != TOK_EOF) {
        if (basic_current_token == TOK_NEWLINE) {
            basic_next_token();
            continue;
        }
        
        if (basic_current_token == TOK_IDENTIFIER) {
            // Check for keywords
            if (basic_strcmp(basic_token_ident, "PRINT") || basic_strcmp(basic_token_ident, "?")) {
                basic_next_token();
                basic_execute_print();
            } else if (basic_strcmp(basic_token_ident, "LET")) {
                basic_next_token();
                basic_execute_let();
            } else if (basic_strcmp(basic_token_ident, "IF")) {
                basic_next_token();
                basic_execute_if();
            } else if (basic_strcmp(basic_token_ident, "FOR")) {
                basic_next_token();
                basic_execute_for();
            } else if (basic_strcmp(basic_token_ident, "NEXT")) {
                basic_next_token();
                basic_execute_next();
            } else if (basic_strcmp(basic_token_ident, "WHILE")) {
                basic_next_token();
                basic_execute_while();
            } else if (basic_strcmp(basic_token_ident, "WEND")) {
                basic_next_token();
                // Find matching WHILE and check condition (simplified)
            } else if (basic_strcmp(basic_token_ident, "INPUT")) {
                basic_next_token();
                basic_execute_input();
            } else if (basic_strcmp(basic_token_ident, "REM") || basic_token_ident[0] == '\'') {
                // Comment - skip to end of line
                while (basic_current_token != TOK_NEWLINE && 
                       basic_current_token != TOK_EOF) {
                    basic_next_token();
                }
            } else if (basic_strcmp(basic_token_ident, "END")) {
                break;
            } else {
                // Assume it's LET without keyword
                basic_execute_let();
            }
        } else {
            // Skip unknown tokens
            basic_next_token();
        }
        
        // Skip to end of statement
        while (basic_current_token != TOK_NEWLINE && 
               basic_current_token != TOK_EOF &&
               basic_current_token != TOK_COLON) {
            basic_next_token();
        }
        if (basic_current_token == TOK_COLON) {
            basic_next_token();
        }
    }
    
    return 0;
}

// Load and run from file
int basic_load_and_run(const char* path) {
    fs_node_t* file = fs_resolve_path(path);
    if (!file || file->type != FS_FILE) {
        terminal_writestring_color("BASIC file not found: ", COLOR_RED);
        terminal_writestring(path);
        terminal_writestring_color("\n", COLOR_RED);
        return -1;
    }
    
    uint32_t size = fs_get_file_size(file);
    if (size == 0) {
        return -1;
    }
    
    char* source = (char*)malloc(size + 1);
    if (!source) {
        return -1;
    }
    
    fs_read_file(file, (uint8_t*)source, size);
    source[size] = '\0';
    
    int result = basic_execute(source);
    free(source);
    return result;
}

// Cleanup
void basic_cleanup(void) {
    // Variables are cleaned up when malloc_reset is called
    malloc_reset();
}

