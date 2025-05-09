__host char debug_m[PERDPU_LOG_SIZE];

__host int debug_offset = 0;

MUTEX_INIT(log_mtx);

void delay(int ms) {
    for (int ii = 0; ii < ms * 4; ii++) {
        for (int i = 1; i < 2; i++) {
            for (int j = 1; j < 2; j++) {
                int k = 3;
                for (int l = 0; l < MY_PQ_M; l++) {
                    justaddtime.cycles +=
                            (i + j + k + l) % 100 + ii * j * k * l % 99;
                }
            }
        }
    }
}

void debug_print(char* str, int len) {
    mutex_lock(log_mtx);

    // if (print_log == 0) {
    //     mutex_unlock(log_mtx);
    //     return;
    // }

    for (int i = 0; i < len; i++) {
        debug_m[debug_offset++] = str[i];

        if (debug_offset >= PERDPU_LOG_SIZE) {
            debug_offset = 0;
        }
    }
    debug_m[debug_offset++] = '\n';

    if (debug_offset >= PERDPU_LOG_SIZE) {
        debug_offset = 0;
    }

    mutex_unlock(log_mtx);

    // delay(10);
}

void floatToString(float num, char* str, int* len, int decimal_places) {
    
    int is_negative = 0;
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }

    int integer_part = (int)num;
    float decimal_part = num - integer_part;

    int index = 0;
    if (integer_part == 0) {
        str[index++] = '0';
    } else {
        int temp_int = integer_part;
        while (temp_int > 0) {
            str[index++] = temp_int % 10 + '0';
            temp_int /= 10;
        }
    }

    if (is_negative) {
        str[index++] = '-';
    }

    for (int i = 0; i < index / 2; i++) {
        char temp = str[i];
        str[i] = str[index - 1 - i];
        str[index - 1 - i] = temp;
    }

    if (decimal_places > 0) {
        str[index++] = '.';

        for (int i = 0; i < decimal_places; i++) {
            decimal_part *= 10;
            int digit = (int)decimal_part;
            str[index++] = digit + '0';
            decimal_part -= digit;
        }
    }

    str[index] = '\0';
    *len = index;
}

void intToString(int num, char* str, int* len) {
    int is_negative = 0;
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }
    int index = 0;
    do {
        str[index++] = num % 10 + '0';
        num /= 10;
    } while (num > 0);
    if (is_negative) {
        str[index++] = '-';
    }
    *len = index;
    for (int i = 0; i < index / 2; i++) {
        char temp = str[i];
        str[i] = str[index - 1 - i];
        str[index - 1 - i] = temp;
    }
    str[index] = '\0';
}


/*-----------line_id: log------------*/

void log_int(int line_id, int log, char* str, int len_str) {
    // if (print_log == 0) {
    //     return;
    // }
    char combined_str[100];
    char line_str[20]; 
    char log_str[20];  
    int line_len, log_len;

    intToString(line_id, line_str, &line_len);
    intToString(log, log_str, &log_len);

    int total_len = line_len + log_len + 2 + len_str;

    for (int i = 0; i < line_len; i++) {
        combined_str[i] = line_str[i];
    }
    combined_str[line_len] = ':';
    combined_str[line_len + 1] = ' ';
    for (int i = 0; i < log_len; i++) {
        combined_str[line_len + 2 + i] = log_str[i];
    }

    for (int i = 0; i < len_str; i++) {
        combined_str[line_len + 2 + log_len + i] = str[i];
    }

    debug_print(combined_str, total_len);
}

void log_float(int line_id, float log, char* str, int len_str) {
    char combined_str[100];
    char line_str[20];  // used to store line number
    char float_str[20]; // used to store float number
    int line_len, float_len;

    
    intToString(line_id, line_str, &line_len);


    floatToString(log, float_str, &float_len, 6);


    int total_len = line_len + float_len + 2 + len_str;


    for (int i = 0; i < line_len; i++) {
        combined_str[i] = line_str[i];
    }


    combined_str[line_len] = ':';
    combined_str[line_len + 1] = ' ';


    for (int i = 0; i < float_len; i++) {
        combined_str[line_len + 2 + i] = float_str[i];
    }

    for (int i = 0; i < len_str; i++) {
        combined_str[line_len + 2 + float_len + i] = str[i];
    }


    debug_print(combined_str, total_len);
}

