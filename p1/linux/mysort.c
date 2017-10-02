#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

typedef struct string_array {
    char **arr;
    int capacity;
    int size;
} string_array;

void init_array(string_array *s) {
    s->capacity = 10;
    s->arr = malloc(10 * sizeof(char*));
    if (s->arr == NULL) {
        perror("malloc() fail");
        exit(1);
    }
    s->size = 0;
}

void expand_arr(string_array *s) {
    s->arr = realloc(s->arr, s->capacity*2*sizeof(char*));
    if (s->arr == NULL) {
        perror("realloc() fail");
        exit(1);
    }
    s->capacity *= 2;
}

void read_string(string_array *s, FILE *f) {
    char buf[1100];
    while(1) {
        if (fgets(buf, 1100, f) == NULL) break;
        int length = strlen(buf);
        char *str = malloc((length+1) * sizeof(char));
        if (str == NULL) {
            perror("malloc() fail");
            exit(1);
        }
        strncpy(str, buf, length+1);
        if (s->size+2>=s->capacity)
            expand_arr(s);
        s->arr[s->size++] = str;
    }
}

void free_string(string_array *s) {
    int i;
    for (i = 0; i<s->size;i++) {
        free(s->arr[i]);
    }
    free(s->arr);
}

void print_string(string_array *s, int limit) {
    int len = s->size;
    if (limit>=0&&limit<len) len = limit;
    int i;
    for (i=0;i<len;i++){
        printf("%s", s->arr[i]);
    }
}

int n = -1;
FILE *read_file;
int reversed = 0;

int comparator(const void *a, const void *b) {
    return strcmp(*(const char**)a, *(const char**)b)*(reversed?-1:1);
}

void print_usage_exit() {
    fprintf(stderr, "Usage: ./mysort [-r] [-n number] [file]\n");
    exit(1);
}

void parse_options(int argc, char *argv[]) {
    char optchr;
    while((optchr = getopt(argc, argv, ":rn:"))!=-1) {
        switch (optchr) {
            case '?':
                fprintf(stderr, "Unknown argument: -%c\n", optopt);
                print_usage_exit();
            case ':':
                fprintf(stderr, "Missing number\n");
                print_usage_exit();
            case 'r':
                reversed = 1;
                break;
            case 'n': {
                char *endptr;
                n = strtol(optarg, &endptr, 10);
                if (*optarg == '\0' || *endptr != '\0') {
                    fprintf(stderr, "Wrong number format\n");
                    print_usage_exit();
                }
                if (n < 0) {
                    fprintf(stderr, "Cannot be negative number\n");
                    print_usage_exit();
                }
                break;
            }
            default:
                perror("getopt error");
                exit(1);
        }
    }
    //printf("optind = %d\n", optind);
    if (optind >= argc) {
        read_file = stdin;
    } else if (optind < argc-1) {
        fprintf(stderr, "Redundant argument.\n");
        print_usage_exit();
    } else {
        read_file = fopen(argv[optind], "r");
        if (read_file == NULL) {
            perror("Failed to open file");
            exit(1);
        }
    }
}

int main(int argc, char *argv[]) {
    parse_options(argc, argv);
    string_array s;
    init_array(&s);
    read_string(&s, read_file);
    qsort(s.arr, s.size, sizeof(char*), comparator);
    print_string(&s,n);
    free_string(&s);
    if (read_file != stdin) fclose(read_file);
    return 0;
}
