/**
 * buffer.c - buffer con acceso directo (útil para I/O) que mantiene
 *            mantiene puntero de lectura y de escritura.
 */
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#include "buffer.h"

inline void buffer_reset(buffer *b) {
    b->read  = b->data;
    b->write = b->data;
    b->parsed = b->data;
}

struct buffer * buffer_init(const size_t n) {
    struct buffer * new_buffer = malloc(sizeof(struct buffer));
    new_buffer->data = malloc(n * sizeof(uint8_t));
    new_buffer->limit = new_buffer->data + n;
    buffer_reset(new_buffer);
    return new_buffer;
}

inline bool buffer_can_write(buffer *b) {
    return b->limit - b->write > 0;
}

inline uint8_t *buffer_write_ptr(buffer *b, size_t *nbyte) {
    assert(b->write <= b->limit);
    *nbyte = b->limit - b->write;
    return b->write;
}

inline bool buffer_can_parse(buffer *b) {
    return b->write - b->parsed > 0;
}

inline bool buffer_can_read(buffer *b) {
    return b->write - b->read > 0;
}

inline uint8_t * buffer_read_ptr(buffer *b, size_t *nbyte) {
    assert(b->read <= b->write);
    *nbyte = b->write - b->read;
    return b->read;
}

inline void buffer_write_adv(buffer *b, const ssize_t bytes) {
    if(bytes > -1) {
        b->write += (size_t) bytes;
        assert(b->write <= b->limit); //se rompe si se pasa?
    }
}

inline void buffer_read_adv(buffer *b, const ssize_t bytes) {
    if(bytes > -1) {
        b->read += (size_t) bytes;
        assert(b->read <= b->write);

        if(b->read == b->write) {
            // compactacion poco costosa
            buffer_compact(b);
        }
    }
}

inline void buffer_parse_adv(buffer *b, const ssize_t bytes) {
    if (bytes > -1) {
        b->parsed += (size_t) bytes;
        assert(b->parsed <= b->write);
        // if (b->parsed == b->write) {
        //     b->parsed = b->data;
        // }
    }
}

inline uint8_t buffer_read(buffer *b) {
    uint8_t ret;
    if(buffer_can_read(b)) {
        ret = *b->read;
        buffer_read_adv(b, 1);
    } else {
        ret = 0;
    }
    return ret;
}

inline uint8_t buffer_parse(buffer *b) {
    uint8_t ret;
    if(buffer_can_parse(b)) {
        ret = *b->parsed;
        buffer_parse_adv(b, 1);
    } else {
        ret = 0;
    }
    return ret;
}

inline void buffer_write(buffer *b, uint8_t c) {
    if(buffer_can_write(b)) {
        *b->write = c;
        buffer_write_adv(b, 1);
    }
}

void buffer_compact(buffer *b) {
    if(b->data == b->read) {
        // nada por hacer
    } else if(b->read == b->write) {
        b->read  = b->data;
        b->write = b->data;
    } else {
        const size_t n = b->write - b->read;
        memmove(b->data, b->read, n);
        b->read  = b->data;
        b->write = b->data + n;
    }
}

void buffer_delete(buffer *b) {
    if (b == NULL) {
        return;
    }
    free(b->data);
    free(b);
}

void buffer_parse_reset(buffer *b){
    if(b->parsed == b->write)
        b->parsed = b->data;
    return;
}