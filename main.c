#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

struct MemoryStruct {
  char *memory;
  size_t size;
};

// Callback function to write data to memory
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  // Reallocate memory to accommodate new data
  char *temp = realloc(mem->memory, mem->size + realsize + 1); // +1 for null-terminator
  if (temp == NULL) {
    fprintf(stderr, "Not enough memory (realloc returned NULL)\n");
    return 0; // Fail to indicate that data was not written
  }

  mem->memory = temp;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0; // Null-terminate the string

  return realsize;
}

char* read_rss(const char *url) {
  CURL *curl;
  CURLcode res;
  struct MemoryStruct chunk;
  chunk.memory = malloc(1);
  chunk.size = 0;

  if(chunk.memory == NULL) {
    fprintf(stderr, "Not enough memory\n");
    return NULL;
  }

  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if(curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
      fprintf(stderr, "Curl failed: %s\n", curl_easy_strerror(res));
      return NULL;
    }

    curl_easy_cleanup(curl);
  }
  curl_global_cleanup();

  return chunk.memory;
}

int parse_xml(const char *raw) {
  xmlDoc *doc = NULL;
  xmlNode *root = NULL;

  doc = xmlReadMemory(raw, (int)strlen(raw), NULL, NULL, 0);
  if(doc == NULL) {
    fprintf(stderr, "XML parse failed\n");
    return 1;
  }

  root = xmlDocGetRootElement(doc);

  printf("Root: %s\n", root->name);

  xmlFreeDoc(doc);
  xmlCleanupParser();

  return 0;
}

int main() {
  char *rss_xml = read_rss("https://hnrss.org/best");

  if(rss_xml == NULL) {
    return 1;
  }

  parse_xml(rss_xml);

  free(rss_xml);

  return 0;
}