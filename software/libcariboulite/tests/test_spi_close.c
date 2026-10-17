#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <time.h>
#include <string.h>
#include "io_utils/io_utils_spi.h"
static io_utils_spi_st dev;
static atomic_int entered, release_transfer, freed;
void __wrap_io_utils_set_gpio_mode(int pin,io_utils_alt_en mode) {(void)pin;(void)mode;}
void __wrap_spi_free(spi_t *spi) {
    (void)spi;
    assert(pthread_mutex_trylock(&dev.mtx)==EBUSY);
    atomic_fetch_add(&freed,1);
}
static void pause_ms(int ms) {struct timespec t={ms/1000,(ms%1000)*1000000L};nanosleep(&t,NULL);}
int __wrap_spi_exchange(spi_t *spi,void *rx,const void *tx,int n) {
    (void)spi; atomic_store(&entered,1);
    while(!atomic_load(&release_transfer))pause_ms(1);
    memcpy(rx,tx,n);return n;
}
static void *transfer(void *arg) {
    (void)arg;unsigned char tx[2]={1,2},rx[2];
    assert(io_utils_spi_transmit(&dev,7,tx,rx,2,io_utils_spi_read_write)==0);
    return NULL;
}
static void *closer(void *arg) {(void)arg;assert(io_utils_spi_close(&dev)==0);return NULL;}
static double now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void) {
    assert(io_utils_spi_init(&dev)==0);
    // A sparse chip table must close slot 7 even though num_of_chips is 1.
    dev.chips[7].initialized=1;dev.chips[7].is_hard_spi=1;
    dev.chips[7].chip_type=io_utils_spi_chip_type_modem;dev.num_of_chips=1;
    dev.current_chip=&dev.chips[7];
    assert(io_utils_spi_init(&dev)==0 && dev.num_of_chips==1);
    pthread_t tx,close_thread;assert(pthread_create(&tx,NULL,transfer,NULL)==0);
    while(!atomic_load(&entered))pause_ms(1);
    double start=now();assert(io_utils_spi_close(&dev)==-1);
    double elapsed=now()-start;assert(elapsed>=0.9 && elapsed<3);
    assert(dev.initialized && dev.chips[7].initialized && !atomic_load(&freed));
    assert(pthread_create(&close_thread,NULL,closer,NULL)==0);
    pause_ms(100);assert(!atomic_load(&freed));
    atomic_store(&release_transfer,1);
    pthread_join(tx,NULL);pthread_join(close_thread,NULL);
    assert(!dev.initialized && atomic_load(&freed)==1 && dev.num_of_chips==0);
    assert(io_utils_spi_close(&dev)==-1);
    unsigned char byte=0;
    assert(io_utils_spi_transmit(&dev,7,&byte,&byte,1,io_utils_spi_read_write)==-1);
    assert(io_utils_spi_remove_chip(&dev,7)==-1);
    for(int i=0;i<20;i++){assert(io_utils_spi_init(&dev)==0);assert(io_utils_spi_close(&dev)==0);}
    puts("PASS SPI close: busy timeout preserves device, in-flight transfer finishes, sparse cleanup, closed calls, reinit");
}
