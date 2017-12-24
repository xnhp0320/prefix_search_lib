#ifdef TEST_CHECK
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <time.h>
#include "bitmap_v4.h"
#include "bitmap_v6.h"


#define RAND_SIZE 1000000

#if __MACH__ 
#include <mach/mach_time.h>
#define ORWL_NANO (+1.0E-9)
#define ORWL_GIGA UINT64_C(1000000000)

static double orwl_timebase = 0.0;
static uint64_t orwl_timestart = 0;

struct timespec orwl_gettime(void) {
      // be more careful in a multithreaded environement
    if (!orwl_timestart) {
        mach_timebase_info_data_t tb = { 0 };
        mach_timebase_info(&tb);
        orwl_timebase = tb.numer;
        orwl_timebase /= tb.denom;
        orwl_timestart = mach_absolute_time();
    }
    struct timespec t;
    double diff = (mach_absolute_time() - orwl_timestart) * orwl_timebase;
    t.tv_sec = diff * ORWL_NANO;
    t.tv_nsec = diff - (t.tv_sec * ORWL_GIGA);
    return t;
}
#define CLOCK_GETTIME(x) *(x)=orwl_gettime()
#else
#define CLOCK_GETTIME(x) clock_gettime(CLOCK_MONOTONIC, x);
#endif

void print_nhi(void *nhi)
{
    uint64_t key = (uint64_t)nhi;
    printf("%llu", key);
}

int del_routes(struct mb_node *root, struct mm *m, FILE *fp)
{
    int i = 0;
    char *line = NULL;
    ssize_t read = 0;
    size_t len = 0;
    
    uint32_t ip = 0;
    uint32_t org_ip = 0;
    uint32_t cidr;
    char buf[128];

    uint32_t key = 1;
    rewind(fp);

    while((read = getline(&line, &len, fp)) != -1){
        if (i & 0x01) {

            //if (i ==111433){
            //    printf("here\n");
            //}
            cidr = atoi(line);
            org_ip = ip;
            ip = ip & (0xffffffff << (32-cidr));
            //if (ip == 0x7a99ff00){
            //    printf("here\n");
            //}
            //insert_prefix(ip,cidr,(struct next_hop_info*)(key));
            //if (i == 107081) {
            //    printf("here\n");
            //}
            //
            if (bitmap_prefix_exist(root, ip, cidr)){
                bitmap_delete_prefix(root, m, ip, cidr, NULL);
            }
            
            void *a = bitmap_do_search(root, org_ip);
            key = (uint32_t)(uint64_t)a;
            if (key != 0) {
                //printf("line %s, cidr %d key %d, org_ip %x\n", buf, cidr, key,org_ip );
            }
            if (bitmap_prefix_exist(root, ip, cidr)){
                printf("prefix exist ! error\n");
            }
        }
        else {
            //printf("line %s", line);
            ip = inet_network(line);
            strcpy(buf, line);
        }
        
        i++;
        //if (i == 8179) {
        //    printf("here\n");
        //}

    }
    printf("del routes %d\n", i/2 );
    rewind(fp);
    
    while((read = getline(&line, &len, fp)) != -1){
        if (i & 0x01) {

            cidr = atoi(line);
            org_ip = ip;
            ip = ip & (0xffffffff << (32-cidr));
            
            void *a = bitmap_do_search(root, org_ip);
            key = (uint32_t)(uint64_t)a;
            if (key != 0) {
                printf("line %s, cidr %d key %d, org_ip %x\n", buf, cidr, key,org_ip );
            }

            //if (i ==111433){
            //    //printf("here\n");
	        //break;
            //}
            //key ++;
        }
        else {
            //printf("line %s", line);
            ip = inet_network(line);
            strcpy(buf, line);
        }
        
        i++;
        //if (i == 8179) {
        //    printf("here\n");
        //}

    }

    return i/2 ;

}

int load_routes(struct mb_node *root, struct mm *m, FILE *fp)
{
    int i = 0;
    char *line = NULL;
    ssize_t read = 0;
    size_t len = 0;
    
    uint32_t ip = 0;
    uint32_t cidr;
    uint64_t key = 1;

    while((read = getline(&line, &len, fp)) != -1){
        if (i & 0x01) {

            //if (i ==111433){
            //    printf("here\n");
            //}
            cidr = atoi(line);
            ip = ip & (0xffffffff << (32-cidr));
            //if (key == 4835){
            //    printf("here\n");
            //}
            bitmap_insert_prefix(root, m, ip, cidr,(void*)(key));

            //if (i ==111433){
            //    //printf("here\n");
	    //    break;
            //}
            key ++;
        }
        else {
            //printf("line %s", line);
            ip = inet_network(line);
        }
        
        i++;
        //if (i == 8179) {
        //    printf("here\n");
        //}

    }
    printf("load routes %d\n", i/2 );
    return i/2 ;
}

#if 1
void test_lookup_valid_batch(struct mb_node *root) 
{
    FILE *fp = fopen("ipv4_fib","r");
    if (fp == NULL)
        exit(-1);

    int i = 0;

    char *line = NULL;
    ssize_t read = 0;
    size_t len = 0;

    uint32_t *ips = (uint32_t *)malloc(1000000 * sizeof(uint32_t));
    uint32_t ip;
    uint32_t key = 1;

    while((read = getline(&line, &len, fp)) != -1){
        if (i & 0x01) {
        }
        else {
            //printf("line %s", line);
            ip = inet_network(line);
            ips[i/2] = ip;
        }
        i++;
    }
    int ip_cnt = i/2;


    int cnt=0;
    void *ret[BATCH];
    struct mb_node *node[BATCH];
    int j;

    for (i = 0; i + BATCH < ip_cnt ;i+=BATCH){
        for(j = 0; j < BATCH; j++) {
            node[j] = root;
        }

        //if (ips[i] == 0x7a99ff00) {
        //    printf("here\n");
        //}
        //struct next_hop_info *a = hash_trie_search(ips[i]);
        
        //if(i == 32) {
        //    printf("here\n");
        //}

        bitmap_do_search_lazy_batch(node, ips + i, ret, BATCH);
        
        for(j = 0; j < BATCH; j++ ) {
            uint32_t b = (uint32_t)(uint64_t)ret[j];
            if ( b == key ) {
                cnt ++;
            }
            else {
                //struct in_addr addr;
                //addr.s_addr = htonl(array[i].test_ip);


                printf("search(0x%x); result %d; i %d\n", ips[i + j], b ,i+j);
                //printf("the truth is ip_test %s  key %d ip %x\n", inet_ntoa(addr),array[i].key*2, array[i].ip);
            }
            //printf("search(0x%x);\n", array[i].ip);

            //printf("search(0x%x); reulst %x\n", array[i].ip, b);
            key ++;
        }
    }

    printf("match %d\n", cnt);
    fclose(fp);
    free(ips);
}
#endif

struct ip_prefix {
    uint32_t ip;
    int cidr;
}; 

int compare(const void *a, const void *b)
{
    const struct ip_prefix *p1 = (const struct ip_prefix *)a;
    const struct ip_prefix *p2 = (const struct ip_prefix *)b;

    if(p1->cidr != p2->cidr) {
        return p2->cidr - p1->cidr;
    }
    else {
        if(p1->ip < p2->ip) return -1;
        if(p1->ip > p2->ip) return 1;
    }
    return 0;
}

int lower_bound(struct ip_prefix *ipp, uint32_t ip, int i, int j)
{
    int s;
    int count, step;
    count = j - i + 1;

    while(count > 0) {
        s = i;
        step = count / 2;
        s += step;
        if (ipp[s].ip < ip) {
            i = ++s;
            count -= step +1;
        } else 
            count = step;
    }
    return s;
}

int binary_search(struct ip_prefix *ipp, uint32_t ip, int i, int j)
{
    int s = lower_bound(ipp, ip, i, j);
    if (s == j+1) return -1;
    if (ipp[s].ip > ip) return -1; 
    return s;
}

int b_search(struct ip_prefix *ipp, uint32_t ip, int *i, int *j)
{
    int k = 32;
    int r1;
    for(; k >= 0; k --) {
        if(i[k] == -1) continue;
        r1 = binary_search(ipp, (ip >> (32-k))<<(32-k), i[k], j[k]);
        if(r1 != -1) return r1 + 1;
    }
    return (r1 == -1) ? 0 : r1 +1;
}

void test_lookup_valid_greedy(void)
{
    struct mb_node root = {0,0,NULL};
    struct mm m;
    memset(&m, 0, sizeof(m));

    FILE *fp = fopen("ipv4_fib","r");
    if (fp == NULL)
        exit(-1);

    int i = 0;

    char *line = NULL;
    ssize_t read = 0;
    size_t len = 0;

    struct ip_prefix *ipp = (struct ip_prefix *)malloc(1000000 * sizeof(struct ip_prefix));
    uint32_t ip;
    int cidr;
    
    int pi[33];
    int pj[33];

    while((read = getline(&line, &len, fp)) != -1){
        if (i & 0x01) {
            cidr = atoi(line);
            ip = ip & (0xffffffff << (32-cidr));
            ipp[i/2].ip = ip;
            ipp[i/2].cidr = cidr;
        }
        else {
            //printf("line %s", line);
            ip = inet_network(line);
        }
        i++;
    }

    qsort(ipp, i/2, sizeof(struct ip_prefix), compare);

    int j;
    for(j = 0; j < 33; j ++) {
	pi[j] = pj[j] = -1;
    }

    int curr = ipp[0].cidr;
    pi[curr]= 0;

    for(j = 1; j < i/2; j++) {
        if(ipp[j].cidr < curr) {
            pj[curr] = j -1;
            curr = ipp[j].cidr;
            pi[curr] = j;
        }
    }
    pj[curr] = j -1;

    for(j = 0; j < i/2 ; j++) {
        bitmap_insert_prefix(&root, &m, ipp[j].ip, ipp[j].cidr, (void*)(unsigned long)(j+1));
    }

    int r1, r2;
    unsigned int k;

    printf("Test greedy \n");
    
    for(k = 0; k < UINT32_MAX; k ++) {
	if(k == 0x1010100) 
	    printf("here\n");
        r1 = b_search(ipp, k, pi, pj); 
        r2 = (int)(unsigned long)bitmap_do_search(&root, k);
        if(r1 != r2) {
            printf("r1 %d r2 %d ip %x \n", r1, r2, k);
            printf("prefix for r1 ip %x cidr %d\n", ipp[r1-1].ip, ipp[r1-1].cidr);
            printf("prefix for r2 ip %x cidr %d\n", ipp[r2-1].ip, ipp[r2-1].cidr);
        }
        if(k % (100000) == 0) {printf("."); fflush(stdout);}
    }
}

void test_lookup_valid(struct mb_node *root) 
{
    FILE *fp = fopen("ipv4_fib","r");
    if (fp == NULL)
        exit(-1);

    int i = 0;

    char *line = NULL;
    ssize_t read = 0;
    size_t len = 0;

    uint32_t *ips = (uint32_t *)malloc(1000000 * sizeof(uint32_t));
    uint32_t ip;
    uint32_t key = 1;

    while((read = getline(&line, &len, fp)) != -1){
        if (i & 0x01) {
        }
        else {
            //printf("line %s", line);
            ip = inet_network(line);
            ips[i/2] = ip;
        }
        i++;
    }
    int ip_cnt = i/2;


    int cnt=0;
    for (i = 0; i< ip_cnt ;i++){

        //if (ips[i] == 0x7a99ff00) {
        //    printf("here\n");
        //}
        //struct next_hop_info *a = hash_trie_search(ips[i]);
        void *a = bitmap_do_search_lazy(root, ips[i]);
        uint32_t b = (uint32_t)(uint64_t)a;
        if ( b == key ) {
            cnt ++;
        }
        else {
            //struct in_addr addr;
            //addr.s_addr = htonl(array[i].test_ip);


            printf("search(0x%x); result %d; i %d\n", ips[i], b ,i);
            //printf("the truth is ip_test %s  key %d ip %x\n", inet_ntoa(addr),array[i].key*2, array[i].ip);
        }
        //printf("search(0x%x);\n", array[i].ip);

        //printf("search(0x%x); reulst %x\n", array[i].ip, b);
        key ++;
    }

    printf("match %d\n", cnt);
    fclose(fp);
    free(ips);
    //mem_usage(&trie->mm);
}

void test_random_ips(struct mb_node *root)
{
    FILE *fp = fopen("ipv4_fib","r");
    if (fp == NULL)
        exit(-1);

    char *line = NULL;
    ssize_t read = 0;
    size_t len = 0;
    int cnt = 0;
    int i = 0;
    uint32_t ip;

    uint32_t *random_ips = (uint32_t *)malloc(RAND_SIZE * sizeof(uint32_t));
    uint32_t *ips = (uint32_t *)malloc(RAND_SIZE * sizeof(uint32_t));

    while((read = getline(&line, &len, fp)) != -1){
        if (i & 0x01) {
            //if (i == 201863){
            //    printf("here\n");
            //}
            
            //printf("cidr %d, key %d\n",array[i/2].cidr, array[i/2].key);
        }
        else {
            ip = inet_network(line);
            ips[i/2] = ip;
        }
        //printf("line %s", line);
        
        i++;
        //if (i == 8179) {
        //    printf("here\n");
        //}
    }
    cnt = i/2;


    for (i=0;i<RAND_SIZE;i++){
        random_ips[i] = ips[random()%cnt];
    }
    free(ips);

    struct timespec tp_b;
    struct timespec tp_a;

    //int j;
    //for (j=0;j<10;j++){

    CLOCK_GETTIME(&tp_b);

    for (i=0;i<RAND_SIZE;i++){
        bitmap_do_search_lazy(root, random_ips[i]);
        //hash_trie_search(random_ips[i]);
        //compact_search(random_ips[i]);
    }
 
    CLOCK_GETTIME(&tp_a);
    long nano = (tp_a.tv_nsec > tp_b.tv_nsec) ? (tp_a.tv_nsec -tp_b.tv_nsec) : (tp_a.tv_nsec - tp_b.tv_nsec + 1000000000ULL);
    printf("sec %ld, nano %ld\n", tp_b.tv_sec, tp_b.tv_nsec);
    printf("sec %ld, nano %ld\n", tp_a.tv_sec, tp_a.tv_nsec);
    printf("nano %ld\n", nano);
    printf("per lookup %.2f, speed %.2f\n", (double)nano/RAND_SIZE, 1e9/((double)nano/RAND_SIZE));
    //}
    free(random_ips);
    fclose(fp);

}


void ipv4_test()
{
    //FILE *fp = fopen("rrc00(2013080808).txt.port","r");
    FILE *fp = fopen("ipv4_fib","r");
    if (fp == NULL)
        exit(-1);
    
    struct mb_node root = {0,0,NULL};
    struct mm m;
    struct mb_node *croot;
    memset(&m, 0, sizeof(m));

    printf("ipv4 test\n");

    load_routes(&root, &m, fp);
    show_mm_stat(&m);
    bitmap_compact(&root, &m, &croot);

    test_lookup_valid(croot);
    test_lookup_valid_greedy();
    test_lookup_valid_batch(croot);
    test_random_ips(croot);

    //bitmap_print_all_prefix(&root, print_nhi);
    del_routes(&root, &m, fp);
    show_mm_stat(&m);
    bitmap_destroy_trie(&root, &m, NULL);
    
    printf("\n");

}

void test_lookup_valid_v6(struct mb_node *root, FILE *fp)
{
    struct ip_v6 ip;
    uint32_t i=1;
    
    char *line = NULL;
    ssize_t read = 0;
    size_t len = 0;

    int r;
    int match = 0;


    struct in6_addr addr;
    
    while((read = getline(&line, &len, fp)) != -1){
        line[strlen(line) - 1] = '\0';
        r = inet_pton(AF_INET6, line, (void *)&addr);
        if ( r == 0 ){
            printf("wrong format\n");
            continue;
        }
        hton_ipv6(&addr);
        memcpy(&ip, &addr, sizeof(addr));

        void *ret = bitmapv6_do_search(root, ip);
        uint64_t key = (uint64_t)ret;
        if ( key != i) {
            printf("overlapped or error %s key %d ret %llu\n", line, i, key);
        }
        else {
            match++;
        }
        i++;
    }

    printf("test %d, match %d\n", i -1 , match);
}

int del_routes_v6(struct mb_node *root, struct mm *m, FILE *fp)
{
    struct ip_v6 ip;
    uint32_t cidr;
    char *slash;
    uint32_t i=1;

    char *line = NULL;
    ssize_t read = 0;
    size_t len = 0;
    int r;
   

    char ip_v6[INET6_ADDRSTRLEN]; 
    char prelen[4];

    struct in6_addr addr;
    
    while((read = getline(&line, &len, fp)) != -1){
        slash = strchr(line, '/');
        if(slash == NULL) {
            printf("wrong format line\n");
            continue;
        }

        memcpy(ip_v6, line, slash - line);
        ip_v6[slash - line] ='\0';

        r = inet_pton(AF_INET6, ip_v6, (void *)&addr);
        if ( r == 0 ){
            printf("wrong format\n");
            continue;
        }
        hton_ipv6(&addr);
        memcpy(&ip, &addr, sizeof(addr));

        memcpy(prelen, slash + 1, strlen(line) - (slash - line) -1);
        prelen[strlen(line) - (slash - line) - 1] ='\0';
        cidr = atoi(prelen);

        void *ret = bitmapv6_do_search(root, ip);
        uint64_t key = (uint64_t)ret;
        if ( key != i) {
            //printf("overlapped or error %s key %d ret %d\n", line, i, key);
        }

        r = bitmapv6_prefix_exist(root, ip, cidr);
        if( r != 1) {
            printf("prefix_exist error\n");
            printf("IP v6 addr: %d %s\n", i, line);
        }
        
        bitmapv6_delete_prefix(root, m, ip, cidr, NULL); 
        r = bitmapv6_prefix_exist(root, ip, cidr);
        if ( r == 1) {
            printf("prefix_exist error\n");
        }

        i++;
    }

    return 0;    
}



int load_routes_v6(struct mb_node *node, struct mm *m, FILE *fp)
{
    int i = 0;
    char *line = NULL;
    ssize_t read = 0;
    size_t len = 0;
   
    struct ip_v6 ip;
    uint32_t cidr;
    uint64_t key = 1;
    char *slash;
    

    char ip_v6[INET6_ADDRSTRLEN]; 
    char prelen[4];

    struct in6_addr addr;
    int ret;
    
    while((read = getline(&line, &len, fp)) != -1){
        slash = strchr(line, '/');
        if(slash == NULL) {
            printf("wrong format line\n");
            continue;
        }

        memcpy(ip_v6, line, slash - line);
        ip_v6[slash - line] ='\0';

        ret = inet_pton(AF_INET6, ip_v6, (void *)&addr);
        if (ret == 0) {
            printf("transform fail\n");
            continue;
        }
        hton_ipv6(&addr);
        memcpy(&ip, &addr, sizeof(addr));

        memcpy(prelen, slash + 1, strlen(line) - (slash - line) -1);
        prelen[strlen(line) - (slash - line) - 1] ='\0';
        cidr = atoi(prelen);
        
        bitmapv6_insert_prefix(node, m, ip, cidr, (void *)key);
        key ++;
        i++;
    }
    printf("load routes %d\n", i);
    return i ;
}

void ipv6_test()
{
    FILE *fp = fopen("ipv6_fib","r");
    if (fp == NULL)
        exit(-1);
    
    struct mb_node root = {0,0,NULL};
    struct mm m;
    memset(&m, 0, sizeof(m));
    printf("ipv6 test\n");

    load_routes_v6(&root, &m, fp);
    //bitmapv6_print_all_prefix(&root, print_nhi);

    rewind(fp);
    del_routes_v6(&root, &m, fp);

    bitmapv6_destroy_trie(&root, &m, NULL);

    printf("\n");

}

int main()
{
    ipv4_test();
    ipv6_test();
    return 0;
}

#endif
