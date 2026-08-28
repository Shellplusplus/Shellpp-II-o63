#include "s441_native_fs.h"

#include "s441_firmware_abi.h"

#define S441_O_RDONLY 0
#define S441_DT_DIRECTORY 4u

static void clear_bytes(void *address, uint32_t length) {
    uint8_t *bytes = (uint8_t *)address;
    uint32_t index;
    for (index = 0u; index < length; ++index) bytes[index] = 0u;
}

/* The ET_REL loader does not provide compiler runtime helpers.  Directory
 * page copies can be lowered to this EABI helper even with -fno-builtin. */
void *__aeabi_memcpy4(void *target, const void *source, uint32_t length) {
    uint8_t *output = (uint8_t *)target;
    const uint8_t *input = (const uint8_t *)source;
    uint32_t index;
    for (index = 0u; index < length; ++index) output[index] = input[index];
    return target;
}

static uint32_t text_length(const char *text, uint32_t limit) {
    uint32_t length = 0u;
    if (!text) return 0u;
    while (length < limit && text[length]) ++length;
    return length;
}

static int copy_text(char *target, uint32_t capacity, const char *source) {
    uint32_t index;
    uint32_t length;
    if (!target || !capacity || !source) return S441_FS_ERR_ARGUMENT;
    length = text_length(source, capacity);
    if (length >= capacity) return S441_FS_ERR_PATH;
    for (index = 0u; index < length; ++index) target[index] = source[index];
    target[length] = '\0';
    return S441_FS_OK;
}

static int validate_path(const char *path) {
    uint32_t index;
    uint32_t component = 0u;
    if (!path || path[0] != '/') return S441_FS_ERR_PATH;
    if (!path[1]) return S441_FS_OK;
    for (index = 1u; index < S441_FS_PATH_CAP; ++index) {
        char value = path[index];
        if (!value) {
            if (!component) return S441_FS_ERR_PATH;
            return S441_FS_OK;
        }
        if (value == '/') {
            if (!component || (component == 1u && path[index - 1u] == '.') ||
                    (component == 2u && path[index - 2u] == '.' &&
                    path[index - 1u] == '.')) return S441_FS_ERR_PATH;
            component = 0u;
        } else {
            ++component;
        }
    }
    return S441_FS_ERR_PATH;
}

int s441_fs_parent(const char *path, char *output, uint32_t capacity) {
    uint32_t length;
    uint32_t cut;
    uint32_t index;
    if (validate_path(path) != S441_FS_OK || !output || capacity < 2u)
        return S441_FS_ERR_PATH;
    if (path[1] == '\0') return copy_text(output, capacity, "/");
    length = text_length(path, S441_FS_PATH_CAP);
    cut = length;
    while (cut > 0u && path[cut - 1u] != '/') --cut;
    if (cut <= 1u) return copy_text(output, capacity, "/");
    --cut;
    if (cut + 1u > capacity) return S441_FS_ERR_PATH;
    for (index = 0u; index < cut; ++index) output[index] = path[index];
    output[cut] = '\0';
    return S441_FS_OK;
}

int s441_fs_join(const char *base, const char *name, char *output,
        uint32_t capacity) {
    uint32_t base_length;
    uint32_t name_length;
    uint32_t index;
    if (validate_path(base) != S441_FS_OK || !name || !output)
        return S441_FS_ERR_PATH;
    name_length = text_length(name, S441_FS_NAME_CAP);
    if (!name_length || name_length >= S441_FS_NAME_CAP ||
            (name_length == 1u && name[0] == '.') ||
            (name_length == 2u && name[0] == '.' && name[1] == '.'))
        return S441_FS_ERR_PATH;
    for (index = 0u; index < name_length; ++index)
        if (name[index] == '/') return S441_FS_ERR_PATH;
    base_length = text_length(base, S441_FS_PATH_CAP);
    if (base_length + name_length + (base_length > 1u ? 2u : 1u) > capacity)
        return S441_FS_ERR_PATH;
    for (index = 0u; index < base_length; ++index) output[index] = base[index];
    if (base_length > 1u) output[base_length++] = '/';
    for (index = 0u; index < name_length; ++index)
        output[base_length + index] = name[index];
    output[base_length + name_length] = '\0';
    return S441_FS_OK;
}

const char *s441_fs_basename(const char *path) {
    uint32_t index;
    uint32_t last = 0u;
    if (validate_path(path) != S441_FS_OK) return 0;
    for (index = 0u; path[index]; ++index)
        if (path[index] == '/') last = index + 1u;
    return path + last;
}

static void append_char(char **cursor, char *end, char value) {
    if (*cursor < end) *(*cursor)++ = value;
}

static void append_text(char **cursor, char *end, const char *text) {
    if (!text) return;
    while (*text && *cursor < end) *(*cursor)++ = *text++;
}

static void append_line(char **cursor, char *end, const char *text) {
    append_text(cursor, end, text);
    append_char(cursor, end, '\n');
}

static void append_unsigned(char **cursor, char *end, uint32_t value) {
    char digits[10];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(digits));
    while (count) append_char(cursor, end, digits[--count]);
}

static uint8_t is_digit(char value) {
    return value >= '0' && value <= '9';
}

static const char *find_text(const char *text, const char *needle) {
    uint32_t index;
    uint32_t needle_length = text_length(needle, 64u);
    if (!text || !needle || !needle_length) return 0;
    for (; *text; ++text) {
        for (index = 0u; index < needle_length; ++index) {
            if (text[index] != needle[index]) break;
        }
        if (index == needle_length) return text;
    }
    return 0;
}

static uint32_t parse_decimal_values(const char *text, uint32_t values[2]) {
    uint32_t count = 0u;
    if (!text) return 0u;
    while (*text && count < 2u) {
        uint32_t whole = 0u;
        uint32_t fraction = 0u;
        uint32_t fraction_digits = 0u;
        uint8_t seen_dot = 0u;
        while (*text && !is_digit(*text) && *text != '.') ++text;
        if (!*text) break;
        while (*text) {
            if (is_digit(*text)) {
                if (!seen_dot) {
                    if (whole < 42949u)
                        whole = whole * 10u + (uint32_t)(*text - '0');
                } else if (fraction_digits < 3u) {
                    fraction = fraction * 10u + (uint32_t)(*text - '0');
                    ++fraction_digits;
                }
                ++text;
            } else if (*text == '.' && !seen_dot) {
                seen_dot = 1u;
                ++text;
            } else {
                break;
            }
        }
        while (fraction_digits < 3u) {
            fraction *= 10u;
            ++fraction_digits;
        }
        values[count] = whole > 42949u ? 42949672u :
            whole * 1000u + fraction;
        if (values[count] > 42949672u) values[count] = 42949672u;
        ++count;
    }
    return count;
}

static uint8_t copy_error(char *text, uint32_t capacity, const char *message) {
    uint32_t index = 0u;
    if (!text || !capacity) return 0u;
    while (message[index] && index + 1u < capacity) {
        text[index] = message[index];
        ++index;
    }
    text[index] = '\0';
    return 1u;
}

int s441_fs_read_text(const char *path, char *text, uint32_t capacity,
        uint32_t *text_length_out) {
    int32_t fd;
    int32_t read_count;
    int32_t close_result;
    uint32_t total = 0u;
    uint8_t probe;
    int result = S441_FS_OK;

    if (!path || !text || capacity < 2u) return S441_FS_ERR_ARGUMENT;
    text[0] = '\0';
    if (text_length_out) *text_length_out = 0u;
    fd = S441_OPEN(path, S441_O_RDONLY, 0);
    if (fd < 0) return S441_FS_ERR_OPEN;
    while (total + 1u < capacity) {
        read_count = S441_READ(fd, text + total, capacity - 1u - total);
        if (read_count < 0 || (uint32_t)read_count > capacity - 1u - total) {
            result = S441_FS_ERR_READ;
            break;
        }
        if (read_count == 0) break;
        total += (uint32_t)read_count;
    }
    if (result == S441_FS_OK && total + 1u == capacity) {
        read_count = S441_READ(fd, &probe, 1u);
        if (read_count < 0 || read_count > 1) result = S441_FS_ERR_READ;
        else if (read_count != 0) result = S441_FS_ERR_TRUNCATED;
    }
    close_result = S441_CLOSE(fd);
    if (result == S441_FS_OK && close_result < 0) result = S441_FS_ERR_CLOSE;
    text[total] = '\0';
    if (text_length_out) *text_length_out = total;
    return result;
}

int s441_fs_read_text_page(const char *path, uint32_t offset, char *text,
        uint32_t capacity, uint32_t *text_length_out, uint8_t *has_next) {
    uint8_t discard[64];
    uint8_t probe;
    int32_t fd;
    int32_t read_count;
    int32_t close_result;
    uint32_t remaining = offset;
    uint32_t total = 0u;
    int result = S441_FS_OK;

    if (!path || !text || capacity < 2u) return S441_FS_ERR_ARGUMENT;
    text[0] = '\0';
    if (text_length_out) *text_length_out = 0u;
    if (has_next) *has_next = 0u;
    fd = S441_OPEN(path, S441_O_RDONLY, 0);
    if (fd < 0) return S441_FS_ERR_OPEN;
    while (remaining) {
        uint32_t request = remaining < sizeof(discard) ? remaining : sizeof(discard);
        read_count = S441_READ(fd, discard, request);
        if (read_count < 0 || (uint32_t)read_count > request) {
            result = S441_FS_ERR_READ;
            break;
        }
        if (!read_count) break;
        remaining -= (uint32_t)read_count;
    }
    if (result == S441_FS_OK && remaining) result = S441_FS_OK;
    while (result == S441_FS_OK && !remaining && total + 1u < capacity) {
        read_count = S441_READ(fd, text + total, capacity - 1u - total);
        if (read_count < 0 || (uint32_t)read_count > capacity - 1u - total) {
            result = S441_FS_ERR_READ;
            break;
        }
        if (!read_count) break;
        total += (uint32_t)read_count;
    }
    if (result == S441_FS_OK && !remaining && total + 1u == capacity) {
        read_count = S441_READ(fd, &probe, 1u);
        if (read_count < 0 || read_count > 1) result = S441_FS_ERR_READ;
        else if (read_count && has_next) *has_next = 1u;
    }
    close_result = S441_CLOSE(fd);
    if (result == S441_FS_OK && close_result < 0) result = S441_FS_ERR_CLOSE;
    text[total] = '\0';
    if (text_length_out) *text_length_out = total;
    return result;
}

int s441_fs_list_directory_page(const char *path, uint32_t start,
        struct s441_fs_directory_page *page) {
    void *directory;
    uint8_t *raw;
    uint32_t ordinal = 0u;
    int32_t close_result;

    if (validate_path(path) != S441_FS_OK || !page) return S441_FS_ERR_ARGUMENT;
    directory = S441_OPENDIR(path);
    if (!directory) return S441_FS_ERR_DIRECTORY;
    clear_bytes(page, sizeof(*page));
    page->start = start;
    page->has_previous = start != 0u;
    while ((raw = S441_READDIR(directory)) != 0) {
        const char *name = (const char *)(raw + 1u);
        uint32_t name_length = text_length(name, S441_FS_NAME_CAP);
        struct s441_fs_entry *entry;
        if (!name_length || name_length >= S441_FS_NAME_CAP ||
                (name_length == 1u && name[0] == '.') ||
                (name_length == 2u && name[0] == '.' && name[1] == '.'))
            continue;
        if (ordinal++ < start) continue;
        if (page->count >= S441_FS_DIR_PAGE_ENTRIES) {
            page->has_next = 1u;
            break;
        }
        entry = &page->entries[page->count++];
        (void)copy_text(entry->name, sizeof(entry->name), name);
        entry->type = raw[0];
        entry->is_directory = raw[0] == S441_DT_DIRECTORY;
    }
    close_result = S441_CLOSEDIR(directory);
    if (close_result < 0) return S441_FS_ERR_CLOSE;
    return S441_FS_OK;
}

int s441_fs_read_cpu(char *text, uint32_t capacity, uint32_t *percent) {
    static const char cpu_path[] = "/proc/cpuload";
    char raw[128];
    uint32_t values[2];
    uint32_t value_count;
    uint32_t result;
    char *cursor;
    char *end;
    int read_result;

    if (!text || capacity < 2u) return S441_FS_ERR_ARGUMENT;
    if (percent) *percent = 0u;
    read_result = s441_fs_read_text(cpu_path, raw, sizeof(raw), 0);
    if (read_result != S441_FS_OK && read_result != S441_FS_ERR_TRUNCATED) {
        (void)copy_error(text, capacity, "CPU: unavailable");
        return read_result;
    }
    value_count = parse_decimal_values(raw, values);
    if (!value_count) {
        (void)copy_error(text, capacity, "CPU: unavailable");
        return S441_FS_ERR_FORMAT;
    }
    result = values[0] / 1000u;
    if (value_count == 2u && values[0]) result = values[1] * 100u / values[0];
    if (result > 100u) result = 100u;
    cursor = text;
    end = text + capacity - 1u;
    append_text(&cursor, end, "CPU: ");
    append_unsigned(&cursor, end, result);
    append_char(&cursor, end, '%');
    *cursor = '\0';
    if (percent) *percent = result;
    return S441_FS_OK;
}

static uint32_t find_named_number(const char *source, const char *name,
        uint8_t *found) {
    const char *position = find_text(source, name);
    uint32_t value = 0u;
    if (found) *found = 0u;
    if (!position) return 0u;
    position += text_length(name, 32u);
    while (*position && !is_digit(*position)) ++position;
    while (is_digit(*position)) {
        if (value <= 429496729u) value = value * 10u + (uint32_t)(*position - '0');
        ++position;
    }
    if (found) *found = 1u;
    return value;
}

static uint8_t find_umem_values(const char *source, uint32_t *total,
        uint32_t *used) {
    const char *line = source;
    while (line && *line) {
        const char *line_end = line;
        const char *cursor = line;
        uint32_t values[3];
        uint32_t count = 0u;
        while (*line_end && *line_end != '\n' && *line_end != '\r') ++line_end;
        if (find_text(line, "Umem") == line ||
                (find_text(line, "Umem") && find_text(line, "Umem") < line_end)) {
            while (cursor < line_end && count < 3u) {
                uint32_t value = 0u;
                while (cursor < line_end && !is_digit(*cursor)) ++cursor;
                while (cursor < line_end && is_digit(*cursor)) {
                    if (value <= 429496729u)
                        value = value * 10u + (uint32_t)(*cursor - '0');
                    ++cursor;
                }
                if (value || count) values[count++] = value;
            }
            if (count >= 2u && values[0] >= values[1]) {
                *total = values[0] / 1024u;
                *used = values[1] / 1024u;
                return *total != 0u;
            }
        }
        while (*line_end == '\n' || *line_end == '\r') ++line_end;
        line = line_end;
    }
    return 0u;
}

/* The allocator row can occur after the small standard meminfo header. Scan
 * the complete procfs stream without retaining it, so a long file cannot hide
 * the authoritative O63 Umem values behind a truncated buffer. */
static int read_live_umem(uint32_t *total, uint32_t *used) {
    static const char memory_path[] = "/proc/meminfo";
    uint8_t chunk[96];
    uint32_t numbers[3];
    uint32_t number = 0u;
    uint32_t number_count = 0u;
    uint8_t number_active = 0u;
    uint8_t umem_state = 0u;
    int32_t fd = S441_OPEN(memory_path, S441_O_RDONLY, 0);
    if (fd < 0) return S441_FS_ERR_OPEN;
    for (;;) {
        int32_t count = S441_READ(fd, chunk, sizeof(chunk));
        uint32_t index;
        if (count < 0 || (uint32_t)count > sizeof(chunk)) {
            (void)S441_CLOSE(fd);
            return S441_FS_ERR_READ;
        }
        if (!count) break;
        for (index = 0u; index < (uint32_t)count; ++index) {
            uint8_t value = chunk[index];
            if (value == '\n' || value == '\r') {
                if (number_active && number_count < 3u)
                    numbers[number_count++] = number;
                if (umem_state == 4u && number_count >= 2u &&
                        numbers[0] >= numbers[1]) {
                    *total = numbers[0] / 1024u;
                    *used = numbers[1] / 1024u;
                    (void)S441_CLOSE(fd);
                    return *total ? S441_FS_OK : S441_FS_ERR_FORMAT;
                }
                number = 0u;
                number_count = 0u;
                number_active = 0u;
                umem_state = 0u;
                continue;
            }
            if (value >= '0' && value <= '9') {
                if (!number_active) {
                    number_active = 1u;
                    number = 0u;
                }
                if (number <= 429496729u)
                    number = number * 10u + (uint32_t)(value - '0');
            } else if (number_active) {
                if (number_count < 3u) numbers[number_count++] = number;
                number = 0u;
                number_active = 0u;
            }
            if (umem_state != 4u) {
                if (umem_state == 0u && value == 'U') umem_state = 1u;
                else if (umem_state == 1u && value == 'm') umem_state = 2u;
                else if (umem_state == 2u && value == 'e') umem_state = 3u;
                else if (umem_state == 3u && value == 'm') umem_state = 4u;
                else if (value == 'U') umem_state = 1u;
                else if (value != ' ' && value != '\t') umem_state = 0u;
            }
        }
    }
    if (number_active && number_count < 3u)
        numbers[number_count++] = number;
    if (umem_state == 4u && number_count >= 2u &&
            numbers[0] >= numbers[1]) {
        *total = numbers[0] / 1024u;
        *used = numbers[1] / 1024u;
        if (S441_CLOSE(fd) < 0) return S441_FS_ERR_CLOSE;
        return *total ? S441_FS_OK : S441_FS_ERR_FORMAT;
    }
    if (S441_CLOSE(fd) < 0) return S441_FS_ERR_CLOSE;
    return S441_FS_ERR_FORMAT;
}

static void append_kb(char **cursor, char *end, uint32_t kb) {
    if (kb >= 1024u) {
        append_unsigned(cursor, end, kb / 1024u);
        append_char(cursor, end, '.');
        append_unsigned(cursor, end, (kb % 1024u) * 10u / 1024u);
        append_text(cursor, end, "MB");
    } else {
        append_unsigned(cursor, end, kb);
        append_text(cursor, end, "KB");
    }
}

int s441_fs_read_memory(char *text, uint32_t capacity, uint32_t *percent) {
    static const char memory_path[] = "/proc/meminfo";
    char raw[640];
    uint32_t total = 0u;
    uint32_t used = 0u;
    uint32_t available;
    uint32_t free_value;
    uint32_t buffers;
    uint32_t cached;
    uint8_t found_total;
    uint8_t found_available;
    uint8_t found_free;
    uint32_t result;
    char *cursor;
    char *end;
    int read_result;

    if (!text || capacity < 2u) return S441_FS_ERR_ARGUMENT;
    if (percent) *percent = 0u;
    read_result = read_live_umem(&total, &used);
    if (read_result == S441_FS_OK) goto format_memory;
    read_result = s441_fs_read_text(memory_path, raw, sizeof(raw), 0);
    if (read_result != S441_FS_OK && read_result != S441_FS_ERR_TRUNCATED) {
        (void)copy_error(text, capacity, "Memory: unavailable");
        return read_result;
    }
    if (!find_umem_values(raw, &total, &used)) {
        total = find_named_number(raw, "MemTotal:", &found_total);
        available = find_named_number(raw, "MemAvailable:", &found_available);
        free_value = find_named_number(raw, "MemFree:", &found_free);
        buffers = find_named_number(raw, "Buffers:", 0);
        cached = find_named_number(raw, "Cached:", 0);
        if (!found_total || !total) {
            (void)copy_error(text, capacity, "Memory: unavailable");
            return S441_FS_ERR_FORMAT;
        }
        if (!found_available) {
            available = found_free ? free_value : 0u;
            if (buffers <= total - (available > total ? total : available))
                available += buffers;
            if (cached <= total - (available > total ? total : available))
                available += cached;
        }
        if (available > total) available = total;
        used = total - available;
    }
format_memory:
    if (used > total) used = total;
    result = total ? used * 100u / total : 0u;
    if (result > 100u) result = 100u;
    cursor = text;
    end = text + capacity - 1u;
    append_text(&cursor, end, "Memory: ");
    append_kb(&cursor, end, used);
    append_char(&cursor, end, '/');
    append_kb(&cursor, end, total);
    append_char(&cursor, end, ' ');
    append_unsigned(&cursor, end, result);
    append_char(&cursor, end, '%');
    *cursor = '\0';
    if (percent) *percent = result;
    return S441_FS_OK;
}

int s441_fs_read_status(char *text, uint32_t capacity) {
    char cpu[32];
    char memory[64];
    uint32_t cpu_percent = 0u;
    uint32_t memory_percent = 0u;
    char *cursor;
    char *end;
    int cpu_result;
    int memory_result;

    if (!text || capacity < 2u) return S441_FS_ERR_ARGUMENT;
    cpu_result = s441_fs_read_cpu(cpu, sizeof(cpu), &cpu_percent);
    memory_result = s441_fs_read_memory(memory, sizeof(memory),
        &memory_percent);
    cursor = text;
    end = text + capacity - 1u;
    if (cpu_result == S441_FS_OK || cpu_result == S441_FS_ERR_TRUNCATED) {
        append_text(&cursor, end, "CPU  ");
        append_unsigned(&cursor, end, cpu_percent);
        append_line(&cursor, end, "%");
    } else {
        append_line(&cursor, end, cpu);
    }
    if (memory_result == S441_FS_OK ||
            memory_result == S441_FS_ERR_TRUNCATED) {
        append_text(&cursor, end, "Memory  ");
        append_unsigned(&cursor, end, memory_percent);
        append_char(&cursor, end, '%');
    } else {
        append_text(&cursor, end, memory);
    }
    *cursor = '\0';
    if (cpu_result != S441_FS_OK && cpu_result != S441_FS_ERR_TRUNCATED)
        return cpu_result;
    if (memory_result != S441_FS_OK && memory_result != S441_FS_ERR_TRUNCATED)
        return memory_result;
    return S441_FS_OK;
}
