#ifndef ADS_PORTER_VENDOR_H
#define ADS_PORTER_VENDOR_H
struct stemmer;
struct stemmer *create_stemmer(void);
void free_stemmer(struct stemmer *z);
int stem(struct stemmer *z, char *word, int last_index);
#endif
