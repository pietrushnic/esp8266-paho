/* Very basic example that just demonstrates we can run at all!
*/
#include "espressif/esp_common.h"
#include "esp/uart.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/netdb.h"

#include "string.h"
#include "MQTTPacket.h"

static int mysock = SO_ERROR;

int transport_sendPacketBuffer(int sock, unsigned char* buf, int buflen)
{
    int rc = 0;
    rc = write(sock, buf, buflen);
    return rc;
}

/**
  return >=0 for a socket descriptor, <0 for an error code
  @todo Basically moved from the sample without changes, should accomodate same usage for 'sock' for clarity,
  removing indirections
  */
int transport_open(char* addr, int port)
{
    int* sock = &mysock;
    int type = SOCK_STREAM;
    struct sockaddr_in address;
    int rc = -1;
    u8_t family = AF_INET;
    struct addrinfo *result = NULL;
    struct addrinfo hints = {0, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};
    static struct timeval tv;

    *sock = -1;
    if (addr[0] == '[')
        ++addr;

    if ((rc = getaddrinfo(addr, NULL, &hints, &result)) == 0)
    {
        struct addrinfo* res = result;

        /* prefer ip4 addresses */
        while (res)
        {
            if (res->ai_family == AF_INET)
            {
                result = res;
                break;
            }
            res = res->ai_next;
        }

        if (result->ai_family == AF_INET)
        {
            address.sin_port = htons(port);
            address.sin_family = family = AF_INET;
            address.sin_addr = ((struct sockaddr_in*)(result->ai_addr))->sin_addr;
        }
        else
            rc = -1;

        freeaddrinfo(result);
    }

    if (rc == 0)
    {
        *sock = socket(family, type, 0);
        if (*sock != -1)
        {
#if defined(NOSIGPIPE)
            int opt = 1;

            if (setsockopt(*sock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&opt, sizeof(opt)) != 0)
                Log(TRACE_MIN, -1, "Could not set SO_NOSIGPIPE for socket %d", *sock);
#endif

            if (family == AF_INET)
                rc = connect(*sock, (struct sockaddr*)&address, sizeof(address));
        }
    }
    if (mysock == SO_ERROR)
        return rc;

    tv.tv_sec = 1;  /* 1 second Timeout */
    tv.tv_usec = 0;
    setsockopt(mysock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
    return mysock;
}

void task1(void *pvParameters)
{
    xQueueHandle *queue = (xQueueHandle *)pvParameters;
    printf("Hello from task1!\r\n");
    uint32_t count = 0;
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    int rc = 0;
    unsigned char buf[200];
    int buflen = sizeof(buf);
    int mysock = 0;
    MQTTString topicString = MQTTString_initializer;
    char* payload = "mypayload";
    int payloadlen = strlen(payload);
    int len = 0;
    char *host = "test.mosquitto.org";
    int port = 1883;

    mysock = transport_open(host,port);
    if(mysock < 0)
        printf("Unable to create socket: %d\n", mysock);

    printf("Sending to hostname %s port %d\n", host, port);

    data.clientID.cstring = "me";
    data.keepAliveInterval = 20;
    data.cleansession = 1;
    //data.username.cstring = "testuser";
    //data.password.cstring = "testpassword";
    data.MQTTVersion = 4;
    topicString.cstring = "pietrushnic/foobar123";


    while(1) {
        vTaskDelay(100);
        xQueueSend(*queue, &count, 0);
        count++;

        memset(buf, 0, buflen);
        len = MQTTSerialize_connect((unsigned char *)buf, buflen, &data);

        printf("1 len = %d\n", len);

        len += MQTTSerialize_publish((unsigned char *)(buf + len), buflen - len, 0, 0, 0, 0, topicString, (unsigned char *)payload, payloadlen);
        printf("2 len = %d\n", len);

        len += MQTTSerialize_disconnect((unsigned char *)(buf + len), buflen - len);
        printf("3 len = %d\n", len);

        rc = transport_sendPacketBuffer(mysock, buf, len);
        if (rc == len)
            printf("Successfully published\n");
        else
            printf("Publish failed: %d == %d\n", rc, len);
    }
}

void task2(void *pvParameters)
{
    printf("Hello from task 2!\r\n");
    xQueueHandle *queue = (xQueueHandle *)pvParameters;
    while(1) {
        uint32_t count;
        if(xQueueReceive(*queue, &count, 1000)) {
            printf("Got %u\n", count);
        } else {
            printf("No msg :(\n");
        }
    }
}

static xQueueHandle mainqueue;

void user_init(void)
{
    uart_set_baud(0, 115200);
    printf("SDK version:%s\n", sdk_system_get_sdk_version());
    mainqueue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(task1, (signed char *)"tsk1", 256, &mainqueue, 2, NULL);
    xTaskCreate(task2, (signed char *)"tsk2", 256, &mainqueue, 2, NULL);
}
