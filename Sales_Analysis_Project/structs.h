#ifndef STRUCTS_H
#define STRUCTS_H

#define MAX_PRODUCTS 100
#define MAX_CUSTOMERS 200
#define MAX_SALES 10000
#define NAME_LEN 50

typedef struct {
    int prod_id;
    char prod_name[NAME_LEN];
    int prod_price;
} Product;

typedef struct {
    int cust_id;
    char cust_name[NAME_LEN];
} Customer;

typedef struct {
    int sale_id;
    int sale_time;
    int cust_id;
    int prod_id;
} Sale;

#endif