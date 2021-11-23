#include "../pop3filter/include/parser.h"
#include <parser_multiline.h>
#include "pop3nio.h"
#include "../pop3filter/include/stm.h"
#include "../pop3filter/include/selector.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "parser_multiline.h"
#define ATTACHMENT(key) ((struct pop3 *)(key)->data)
int state = 0;

//Dado el mensaje, devuelve el indice donde terminan los headers, y transforma los puntos.
int parse_headers(struct copy * c){
    buffer * b = c->write_b;
    int index = 0;
    size_t count = 0;
    char * read = (char *) buffer_read_ptr(b, &count);
    //int n = strlen(read);

    for(size_t i = 0; i<count; i++){

        switch(state){
            case HEADER:
                index = i;
                if(read[i] == '\r') state = HEADER_CR_1;
                else state = HEADER;
                
                break;
            case HEADER_CR_1:
                index = i;
                if(read[i] == '\n') state = HEADER_N;
                else state = HEADER;
                
                break;
            case HEADER_N:
                index = i;
                if(read[i] == '\r') state = HEADER_CR_2;
                else state = HEADER;
                break;
            case HEADER_CR_2:
                index = i;
                if(read[i] == '\n') {    
                    state = NEW_LINE;
                }else
                state = HEADER;
                break;
            case NEW_LINE:
                if(read[i] == '.') state = DOT;
                else state = BYTE;
                break;
            case  DOT:
                if(read[i] == '\r') state = DOT_CR;
                else state = BYTE;
                break;
            case  DOT_2:
                if((i-1) == 0){
                  strcpy(read,read -1);
                  read[count-1] = 0;
                }else{
                    char * buff = malloc((i-1)* sizeof(char));
                    strncpy(buff, read, i-2);
                    buff[i-1] = 0;
                    strncpy(read + (i-1),buff, count- i + 1);

                }
                if(read[i] == '\r') state = CR;
                else state = BYTE;
                break;
            case DOT_CR:
                if(read[i] == '\n') state = END;
                else state = BYTE;
                break;
            case BYTE:
                if(read[i] == '\r') state = CR;
                else state = BYTE;
                break;
            case CR:
                if(read[i] == '\n') state = NEW_LINE;
                else state = BYTE;
                break;
            case END:
                return index;
                break;
        }

    
    }
    

    return index;

    
}
