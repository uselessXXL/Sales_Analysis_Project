#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "structs.h"

Product products[MAX_PRODUCTS];
int products_total = 0;

Customer customers[MAX_CUSTOMERS];
int customers_total = 0;

Sale sales[MAX_SALES];
int sales_total = 0;

int min_spending = 800;

pthread_mutex_t sync_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *analysis_log = NULL;

void log_message(const char *msg) {
    pthread_mutex_lock(&sync_mutex);
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(analysis_log, "[%s] %s\n", buf, msg);
    fflush(analysis_log);
    pthread_mutex_unlock(&sync_mutex);
}

int load_products() {
    FILE *f = fopen("products.txt", "r");
    if (!f) { printf("Ошибка: products.txt\n"); return 0; }
    products_total = 0;
    while (fscanf(f, "%d;%49[^;];%d\n",
           &products[products_total].prod_id,
           products[products_total].prod_name,
           &products[products_total].prod_price) == 3)
        products_total++;
    fclose(f);
    return 1;
}

int load_customers() {
    FILE *f = fopen("customers.txt", "r");
    if (!f) { printf("Ошибка: customers.txt\n"); return 0; }
    customers_total = 0;
    while (fscanf(f, "%d;%49[^\n]\n",
           &customers[customers_total].cust_id,
           customers[customers_total].cust_name) == 2)
        customers_total++;
    fclose(f);
    return 1;
}

int load_sales() {
    FILE *f = fopen("sales.txt", "r");
    if (!f) { printf("Ошибка: sales.txt\n"); return 0; }
    sales_total = 0;
    while (fscanf(f, "%d;%d;%d;%d\n",
           &sales[sales_total].sale_id,
           &sales[sales_total].sale_time,
           &sales[sales_total].cust_id,
           &sales[sales_total].prod_id) == 4)
        sales_total++;
    fclose(f);
    return 1;
}

int find_price(int prod_id) {
    for (int i = 0; i < products_total; i++)
        if (products[i].prod_id == prod_id) return products[i].prod_price;
    return 0;
}

const char* find_product_name(int prod_id) {
    for (int i = 0; i < products_total; i++)
        if (products[i].prod_id == prod_id) return products[i].prod_name;
    return "Неизвестно";
}

const char* find_customer_name(int cust_id) {
    for (int i = 0; i < customers_total; i++)
        if (customers[i].cust_id == cust_id) return customers[i].cust_name;
    return "Неизвестно";
}

typedef struct {
    int from;
    int to;
    long long sum;
    int prod_count[MAX_PRODUCTS];
    int cust_total[MAX_CUSTOMERS];
} WorkerData;

#define WORKERS 3

void *compute_sum(void *arg) {
    WorkerData *w = (WorkerData *)arg;
    w->sum = 0;
    for (int i = w->from; i < w->to; i++) {
        w->sum += find_price(sales[i].prod_id);
    }
    return NULL;
}

void *compute_popularity(void *arg) {
    WorkerData *w = (WorkerData *)arg;
    memset(w->prod_count, 0, sizeof(w->prod_count));
    for (int i = w->from; i < w->to; i++) {
        for (int p = 0; p < products_total; p++) {
            if (products[p].prod_id == sales[i].prod_id) {
                w->prod_count[p]++;
                break;
            }
        }
    }
    return NULL;
}

void *compute_spending(void *arg) {
    WorkerData *w = (WorkerData *)arg;
    memset(w->cust_total, 0, sizeof(w->cust_total));
    for (int i = w->from; i < w->to; i++) {
        int price = find_price(sales[i].prod_id);
        for (int c = 0; c < customers_total; c++) {
            if (customers[c].cust_id == sales[i].cust_id) {
                w->cust_total[c] += price;
                break;
            }
        }
    }
    return NULL;
}

typedef struct {
    int from;
    int to;
    int hours[24];
} TrendData;

void *compute_trends(void *arg) {
    TrendData *t = (TrendData *)arg;
    memset(t->hours, 0, sizeof(t->hours));
    for (int i = t->from; i < t->to; i++) {
        time_t ts = (time_t)sales[i].sale_time;
        struct tm *tm = localtime(&ts);
        t->hours[tm->tm_hour]++;
    }
    return NULL;
}

void create_report() {
    FILE *report = fopen("report.txt", "w");
    if (!report) { printf("Ошибка создания отчёта\n"); return; }

    fprintf(report, "=== ОТЧЁТ О ПРОДАЖАХ ===\n");
    fprintf(report, "Сформирован: %s", ctime(&(time_t){time(NULL)}));
    fprintf(report, "Всего транзакций: %d\n\n", sales_total);

    pthread_t threads[WORKERS];
    WorkerData results[WORKERS];
    int chunk = sales_total / WORKERS;

    for (int i = 0; i < WORKERS; i++) {
        results[i].from = i * chunk;
        results[i].to = (i == WORKERS - 1) ? sales_total : (i + 1) * chunk;
        pthread_create(&threads[i], NULL, compute_sum, &results[i]);
    }
    long long total = 0;
    for (int i = 0; i < WORKERS; i++) {
        pthread_join(threads[i], NULL);
        total += results[i].sum;
    }
    fprintf(report, "1. СУММА ВСЕХ ПРОДАЖ: %lld руб.\n\n", total);

    WorkerData pop[WORKERS];
    for (int i = 0; i < WORKERS; i++) {
        pop[i].from = i * chunk;
        pop[i].to = (i == WORKERS - 1) ? sales_total : (i + 1) * chunk;
        pthread_create(&threads[i], NULL, compute_popularity, &pop[i]);
    }
    int total_per_product[MAX_PRODUCTS] = {0};
    for (int i = 0; i < WORKERS; i++) {
        pthread_join(threads[i], NULL);
        for (int p = 0; p < products_total; p++)
            total_per_product[p] += pop[i].prod_count[p];
    }

    int sorted[MAX_PRODUCTS];
    for (int i = 0; i < products_total; i++) sorted[i] = i;
    for (int i = 0; i < products_total - 1; i++)
        for (int j = i + 1; j < products_total; j++)
            if (total_per_product[sorted[j]] > total_per_product[sorted[i]]) {
                int tmp = sorted[i]; sorted[i] = sorted[j]; sorted[j] = tmp;
            }

    fprintf(report, "2. ТОП-5 ПОПУЛЯРНЫХ:\n");
    for (int i = 0; i < 5 && i < products_total; i++)
        fprintf(report, "   %d. %s — %d продаж\n",
                i + 1, products[sorted[i]].prod_name, total_per_product[sorted[i]]);

    fprintf(report, "\n   ТОП-5 НЕПОПУЛЯРНЫХ:\n");
    for (int i = 0; i < 5 && i < products_total; i++) {
        int idx = products_total - 1 - i;
        fprintf(report, "   %d. %s — %d продаж\n",
                i + 1, products[sorted[idx]].prod_name, total_per_product[sorted[idx]]);
    }
    fprintf(report, "\n");

    WorkerData spend[WORKERS];
    for (int i = 0; i < WORKERS; i++) {
        spend[i].from = i * chunk;
        spend[i].to = (i == WORKERS - 1) ? sales_total : (i + 1) * chunk;
        pthread_create(&threads[i], NULL, compute_spending, &spend[i]);
    }
    int total_per_customer[MAX_CUSTOMERS] = {0};
    for (int i = 0; i < WORKERS; i++) {
        pthread_join(threads[i], NULL);
        for (int c = 0; c < customers_total; c++)
            total_per_customer[c] += spend[i].cust_total[c];
    }

    fprintf(report, "3. ПОКУПАТЕЛИ СВЫШЕ %d руб.:\n", min_spending);
    int found = 0;
    for (int c = 0; c < customers_total; c++) {
        if (total_per_customer[c] > min_spending) {
            fprintf(report, "   %s (ID=%d) — %d руб.\n",
                    customers[c].cust_name, customers[c].cust_id, total_per_customer[c]);
            found++;
        }
    }
    if (!found) fprintf(report, "   (нет)\n");
    fprintf(report, "\n");

    TrendData trends[WORKERS];
    for (int i = 0; i < WORKERS; i++) {
        trends[i].from = i * chunk;
        trends[i].to = (i == WORKERS - 1) ? sales_total : (i + 1) * chunk;
        pthread_create(&threads[i], NULL, compute_trends, &trends[i]);
    }
    int hourly[24] = {0};
    for (int i = 0; i < WORKERS; i++) {
        pthread_join(threads[i], NULL);
        for (int h = 0; h < 24; h++)
            hourly[h] += trends[i].hours[h];
    }

    fprintf(report, "4. ТЕНДЕНЦИИ (по часам):\n");
    int peak = 0;
    for (int h = 0; h < 24; h++) {
        fprintf(report, "   %02d:00 — %d", h, hourly[h]);
        for (int b = 0; b < hourly[h] && b < 40; b++) fprintf(report, "#");
        fprintf(report, "\n");
        if (hourly[h] > hourly[peak]) peak = h;
    }
    fprintf(report, "\n   Пик: %02d:00\n", peak);

    fclose(report);
    printf("Отчёт создан: report.txt\n");
    log_message("Отчёт сгенерирован");
}

void show_menu() {
    printf("\n===== МЕНЮ =====\n");
    printf("1. Загрузить данные и создать отчёт\n");
    printf("2. Изменить порог (сейчас: %d)\n", min_spending);
    printf("3. Показать статистику\n");
    printf("0. Выход\n");
    printf("Выбор: ");
}

int main() {
    analysis_log = fopen("analyzer.log", "a");
    if (!analysis_log) { printf("Ошибка: analyzer.log\n"); return 1; }
    log_message("=== Анализатор запущен ===");

    if (!load_products()) return 1;
    if (!load_customers()) return 1;

    printf("Товаров: %d, Покупателей: %d\n", products_total, customers_total);

    int choice;
    while (1) {
        show_menu();
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                if (!load_sales()) { printf("Нет данных\n"); break; }
                printf("Продаж: %d\n", sales_total);
                create_report();
                break;
            case 2:
                printf("Новый порог: ");
                scanf("%d", &min_spending);
                log_message("Порог изменён");
                break;
            case 3:
                if (!load_sales()) { printf("Нет данных\n"); break; }
                printf("Всего продаж: %d\n", sales_total);
                break;
            case 0:
                log_message("Завершение");
                fclose(analysis_log);
                return 0;
            default:
                printf("Неверный выбор\n");
        }
    }
    return 0;
}