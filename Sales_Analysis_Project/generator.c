#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include "structs.h"

#define THREAD_COUNT 5

Product products[MAX_PRODUCTS];
int total_products = 0;

pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *sales_output = NULL;
int next_sale_id = 1;

FILE *event_log = NULL;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void record_event(const char *event) {
    pthread_mutex_lock(&log_mutex);
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(event_log, "[%s] %s\n", timestamp, event);
    fflush(event_log);
    pthread_mutex_unlock(&log_mutex);
}

int read_products_file() {
    FILE *file = fopen("products.txt", "r");
    if (!file) {
        printf("Ошибка: products.txt не найден\n");
        return 0;
    }
    total_products = 0;
    while (fscanf(file, "%d;%49[^;];%d\n",
           &products[total_products].prod_id,
           products[total_products].prod_name,
           &products[total_products].prod_price) == 3) {
        total_products++;
    }
    fclose(file);
    return 1;
}

typedef struct {
    int thread_num;
    int product_idx;
    int wait_time;
} ThreadParams;

void *simulate_sale(void *params) {
    ThreadParams *p = (ThreadParams *)params;
    char message[256];

    snprintf(message, sizeof(message), 
             "Поток %d стартовал: продаёт '%s' (id=%d) каждые %d сек",
             p->thread_num, products[p->product_idx].prod_name,
             products[p->product_idx].prod_id, p->wait_time);
    record_event(message);

    while (1) {
        Sale sale;
        pthread_mutex_lock(&write_mutex);
        sale.sale_id = next_sale_id++;
        pthread_mutex_unlock(&write_mutex);

        sale.sale_time = (int)time(NULL);
        sale.cust_id = rand() % 40 + 1;
        sale.prod_id = products[p->product_idx].prod_id;

        pthread_mutex_lock(&write_mutex);
        fprintf(sales_output, "%d;%d;%d;%d\n",
                sale.sale_id, sale.sale_time, 
                sale.cust_id, sale.prod_id);
        fflush(sales_output);
        pthread_mutex_unlock(&write_mutex);

        snprintf(message, sizeof(message),
                 "Поток %d: Продажа #%d — '%s' (покупатель #%d)",
                 p->thread_num, sale.sale_id, 
                 products[p->product_idx].prod_name, sale.cust_id);
        record_event(message);
        printf("%s\n", message);

        sleep(p->wait_time);
    }
    return NULL;
}

int main() {
    srand(time(NULL));

    event_log = fopen("generator.log", "a");
    if (!event_log) {
        printf("Ошибка: generator.log\n");
        return 1;
    }
    record_event("=== Генератор запущен ===");

    if (!read_products_file()) return 1;

    printf("Товаров загружено: %d\n", total_products);

    sales_output = fopen("sales.txt", "a");
    if (!sales_output) {
        printf("Ошибка: sales.txt\n");
        return 1;
    }

    pthread_t threads[THREAD_COUNT];
    ThreadParams params[THREAD_COUNT];

    for (int i = 0; i < THREAD_COUNT && i < total_products; i++) {
        params[i].thread_num = i + 1;
        params[i].product_idx = i;
        params[i].wait_time = rand() % 4 + 1;

        if (pthread_create(&threads[i], NULL, simulate_sale, &params[i]) != 0) {
            printf("Ошибка создания потока %d\n", i + 1);
            return 1;
        }
    }

    for (int i = 0; i < THREAD_COUNT && i < total_products; i++) {
        pthread_join(threads[i], NULL);
    }

    fclose(sales_output);
    fclose(event_log);
    return 0;
}