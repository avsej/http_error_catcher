#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/inet.h>

#include <microhttpd.h>

#include <riemann/event.h>
#include <riemann/message.h>
#include <riemann/client.h>
#include <riemann/attribute.h>

int listen_port = 0, riemann_port = 5555;
char *riemann_host = NULL;
char *maintainance_page = NULL;
const char *riemann_field_service = "http_error_catcher";
const char *riemann_field_host = NULL;
char *body = "OK\n";
int body_len = 3;

typedef struct attribute_list_s
{
    riemann_attribute_pairs_t *attr;
    struct attribute_list_s *next;
} attribute_list_t;

static attribute_list_t *
push_riemann_attribute_pair(attribute_list_t *list,
                            const char *key,
                            const char *value)
{
    riemann_attribute_pairs_t *attr;
    attribute_list_t *item;

    item = calloc(1, sizeof(attribute_list_t));
    if (item == NULL) {
        return NULL;
    }
    attr = calloc(1, sizeof(riemann_attribute_pairs_t));
    if (attr == NULL) {
        free(item);
        return NULL;
    }
    item->attr = attr;
    attr->key = (char *)key;
    attr->value = (char *)value;
    if (list) {
        list->next = item;
    }
    return item;
}

static int
headers_to_attributes(void *cls,
                      enum MHD_ValueKind kind,
                      const char *key,
                      const char *value)
{
    attribute_list_t **cur = cls;

    *cur = push_riemann_attribute_pair(*cur, key, value);
    if (*cur == NULL) {
        return MHD_NO;
    }

    (void)kind;
    return MHD_YES;
}

static int
write_to_riemann(void *cls,
                 struct MHD_Connection *connection,
                 const char *url,
                 const char *method,
                 const char *version,
                 const char *upload_data,
                 size_t *upload_data_size,
                 void **ptr)
{
    int marker, ret;
    riemann_client_t *riemann_client = cls;

    if (*ptr != &marker || *upload_data_size != 0) {
        *ptr = &marker;
        *upload_data_size = 0;
        return MHD_YES;
    }

    {
        riemann_message_t msg = RIEMANN_MSG_INIT;
        riemann_event_t event = RIEMANN_EVENT_INIT;
        riemann_event_t *events[] = { NULL };
        attribute_list_t *head, *cur;
        int header_count = 2, i;
        riemann_attribute_pairs_t *attributes;

        events[0] = &event;
        riemann_event_set_host(&event, riemann_field_host);
        riemann_event_set_service(&event, riemann_field_service);

        head = push_riemann_attribute_pair(NULL, "Method", method);
        if (head == NULL) {
            return MHD_NO;
        }
        cur = push_riemann_attribute_pair(head, "URL", url);
        if (cur == NULL) {
            free(head->attr);
            free(head);
            return MHD_NO;
        }
        header_count += MHD_get_connection_values(connection, MHD_HEADER_KIND,
                                                  headers_to_attributes, &cur);

        attributes = calloc(header_count, sizeof(riemann_attribute_pairs_t));
        if (attributes == NULL) {
            cur = head;
            while (cur) {
                free(cur->attr);
                head = cur;
                free(cur);
                cur = head->next;
            }
            return MHD_NO;
        }
        cur = head;
        for (i = 0; i < header_count && cur; ++i) {
            attributes[i].key = cur->attr->key;
            attributes[i].value = cur->attr->value;
            free(cur->attr);
            head = cur->next;
            free(cur);
            cur = head;
        }
        riemann_event_set_attributes(&event, attributes, header_count);
        riemann_message_set_events(&msg, events, 1);

        ret = riemann_client_send_message(riemann_client, &msg, 0, NULL);
        if (ret) {
            fprintf(stderr, "Cannot send message: %s\n", strerror(errno));
        }
        fprintf(stderr, "Notfied riemann about '%s'\n", url);

        riemann_event_free(&event);
        free(attributes);
    }

    {
        struct MHD_Response *response;
        response = MHD_create_response_from_buffer(body_len, (void *)body,
                                                   MHD_RESPMEM_PERSISTENT);
        if (response) {
            MHD_add_response_header(response, "Content-Type", "text/html");
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);
        }
    }
    (void)version;
    (void)upload_data;
    return ret;
}

void
print_help()
{
    fprintf(stderr, "Usage: http-error-catcher -l 9000 -r localhost [-p 5555] -h example.com [-s http_error_catcher] [-m maintainance.html]\n");
}

void
wait()
{
       pthread_mutex_t mutex;
       pthread_cond_t cond;
       int ret;

       ret = pthread_mutex_init(&mutex, NULL);
       if (ret != 0) {
           fprintf(stderr, "Cannot initialize mutex\n");
           exit(EXIT_FAILURE);
       }
       pthread_mutex_lock(&mutex);
       ret = pthread_cond_init(&cond, NULL);
       if (ret != 0) {
           fprintf(stderr, "Cannot initialize condition variable\n");
           exit(EXIT_FAILURE);
       }
       pthread_cond_wait(&cond, &mutex);

       pthread_mutex_unlock(&mutex);
}

void
init_from_env()
{
    char *str;
    int num;

    str = getenv("LISTEN_PORT");
    if (str != NULL) {
        num = atoi(str);
        if (num > 0) {
            listen_port = num;
        }
    }
    str = getenv("RIEMANN_HOST");
    if (str != NULL && str[0] != '\0') {
        riemann_host = str;
    }
    str = getenv("RIEMANN_PORT");
    if (str != NULL) {
        num = atoi(str);
        if (num > 0) {
            riemann_port = num;
        }
    }
    str = getenv("RIEMANN_FIELD_SERVICE");
    if (str != NULL && str[0] != '\0') {
        riemann_field_service = str;
    }
    str = getenv("RIEMANN_FIELD_HOST");
    if (str != NULL && str[0] != '\0') {
        riemann_field_host = str;
    }
    str = getenv("MAINTAINANCE_PAGE");
    if (str != NULL && str[0] != '\0') {
        maintainance_page = str;
    }
}

int main(int argc, char ** argv)
{
    int ret;
    struct MHD_Daemon *daemon;
    int opt;
    riemann_client_t riemann_client;
    struct sockaddr_in localhost_addr;

    init_from_env();
    while ((opt = getopt(argc, argv, "l:r:p:s:h:m:")) != -1) {
        switch (opt) {
        case 'l':
            listen_port = atoi(optarg);
            break;
        case 'r':
            riemann_host = strdup(optarg);
            break;
        case 'p':
            riemann_port = atoi(optarg);
            break;
        case 's':
            riemann_field_service = strdup(optarg);
            break;
        case 'h':
            riemann_field_host = strdup(optarg);
            break;
        case 'm':
            maintainance_page = strdup(optarg);
            break;
        default:
            print_help();
            exit(EXIT_FAILURE);
        }
    }

    if (!listen_port || !riemann_host || !riemann_field_host) {
        if (!listen_port) {
            fprintf(stderr, "port to listen on is not specified (LISTEN_PORT or -l)\n");
        }
        if (!riemann_host) {
            fprintf(stderr, "riemann host is not specified (RIEMANN_HOST or -r)\n");
        }
        if (!riemann_field_host) {
            fprintf(stderr, "host field for riemann payload is not specified (RIEMANN_FIELD_HOST or -h)\n");
        }
        print_help();
        exit(EXIT_FAILURE);
    }
    if (maintainance_page) {
        int ret;
        int fd;
        struct stat stat;

        fd = open(maintainance_page, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "Cannot open maintainance page '%s': strerror(%s)\n",
                    maintainance_page, strerror(errno));
            exit(EXIT_FAILURE);
        }
        ret = fstat(fd, &stat);
        if (ret != 0) {
            fprintf(stderr, "Cannot get size of maintainance page '%s': strerror(%s)\n",
                    maintainance_page, strerror(errno));
            close(fd);
            exit(EXIT_FAILURE);
        }
        body_len = stat.st_size;
        body = calloc(body_len, sizeof(char));
        if (body == NULL) {
            fprintf(stderr, "Cannot allocate enough memory for content of maintainance page '%s'\n",
                    maintainance_page);
            close(fd);
            exit(EXIT_FAILURE);
        }
        ret = read(fd, body, body_len);
        if (ret != body_len) {
            fprintf(stderr, "Error while reading maintainance page content '%s': strerror(%s)\n",
                    maintainance_page, strerror(errno));
            close(fd);
            exit(EXIT_FAILURE);
        }
    }
    ret = riemann_client_init(&riemann_client);
    if (ret) {
        fprintf(stderr, "Cannot initialize riemann_client: strerror(%s)\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ret = riemann_client_connect(&riemann_client, RIEMANN_TCP, riemann_host, riemann_port);
    if (ret) {
        fprintf(stderr, "Cannot connect: strerror(%s) gai_strerrror(%s)\n", strerror(errno), gai_strerror(ret));
        exit(EXIT_FAILURE);
    }

    memset(&localhost_addr, 0, sizeof(struct sockaddr_in));
    localhost_addr.sin_family = AF_INET;
    localhost_addr.sin_port = htons(listen_port);
    ret = inet_pton(AF_INET, "127.0.0.1", &localhost_addr.sin_addr);
    if (ret != 1) {
        fprintf(stderr, "Cannot construct address structure: strerror(%s)\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_POLL,
                              listen_port, NULL, NULL,
                              &write_to_riemann, &riemann_client,
                              MHD_OPTION_SOCK_ADDR, &localhost_addr,
                              MHD_OPTION_END);
    if (daemon == NULL) {
        fprintf(stderr, "Cannot start HTTP daemon\n");
        exit(EXIT_FAILURE);
    }
    wait();
    MHD_stop_daemon(daemon);
    riemann_client_free(&riemann_client);
    return 0;
}
