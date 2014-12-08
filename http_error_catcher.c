#include <microhttpd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <riemann/event.h>
#include <riemann/message.h>
#include <riemann/client.h>
#include <riemann/attribute.h>

const char *riemann_field_service = "http_error_catcher";
const char *riemann_field_host = NULL;

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
            fprintf(stderr, "Can't send message: %s\n", strerror(errno));
        }
    }

    {
        struct MHD_Response *response;
        response = MHD_create_response_from_buffer(2, "OK", MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
    }
    (void)cls;
    (void)version;
    (void)upload_data;
    (void)upload_data_size;
    return ret;
}

void
print_help()
{
    fprintf(stderr, "Usage: http_error_catcher -l 9000 -r localhost [-p 5555] -h example.com [-s http_error_catcher]\n");
}

int main(int argc, char ** argv)
{
    int ret;
    struct MHD_Daemon *daemon;
    int opt, listen_port = 0, riemann_port = 5555;
    char *riemann_host = NULL;
    riemann_client_t riemann_client;

    while ((opt = getopt(argc, argv, "l:r:p:s:h:")) != -1) {
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
        default:
            print_help();
            exit(EXIT_FAILURE);
        }
    }
    if (!listen_port || !riemann_host || !riemann_field_host) {
        print_help();
        exit(EXIT_FAILURE);
    }
    ret = riemann_client_init(&riemann_client);
    if (ret) {
        fprintf(stderr, "Can't initialize riemann_client: strerror(%s)\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ret = riemann_client_connect(&riemann_client, RIEMANN_UDP, riemann_host, riemann_port);
    if (ret) {
        fprintf(stderr, "Can't connect: strerror(%s) gai_strerrror(%s)\n", strerror(errno), gai_strerror(ret));
        exit(EXIT_FAILURE);
    }

    daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_POLL,
                              listen_port, NULL, NULL,
                              &write_to_riemann, &riemann_client,
                              MHD_OPTION_END);
    if (daemon == NULL) {
        fprintf(stderr, "Cannot start HTTP daemon\n");
        exit(EXIT_FAILURE);
    }
    getc(stdin);
    MHD_stop_daemon(daemon);
    return 0;
}
