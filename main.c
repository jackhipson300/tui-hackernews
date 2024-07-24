#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <ncurses.h>

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


typedef struct {
  char *title;
  char *link;
  char *comments_link;
} Post;

typedef struct {
  Post **arr;
  unsigned int len;
} Posts;

void print_post(Post *post) {
  printf("Title: %s\n", post->title);
  printf("\tLink: %s\n", post->link);
  printf("\tComments: %s\n", post->comments_link);
}

void free_post(Post *post) {
  free(post->title);
  free(post->link);
  free(post->comments_link);
  free(post);
}

Posts* parse_xml(const char *raw) {
  xmlDoc *doc = NULL;

  doc = xmlReadMemory(raw, (int)strlen(raw), NULL, NULL, 0);
  if(doc == NULL) {
    fprintf(stderr, "XML parse failed\n");
    return NULL;
  }

  unsigned int num_items = 0;
  xmlNode *curr = xmlDocGetRootElement(doc);
  curr = curr->xmlChildrenNode->xmlChildrenNode;
  while(curr != NULL) {
    if(!xmlStrcmp(curr->name, (const xmlChar *)"item")) {
      ++num_items;
    }

    curr = curr->next;
  }

  printf("%d\n", num_items);

  Post **posts_arr = malloc(sizeof(Post*) * num_items);

  int i = 0;
  curr = xmlDocGetRootElement(doc)->xmlChildrenNode->xmlChildrenNode;
  while(curr != NULL) {
    if(!xmlStrcmp(curr->name, (const xmlChar *)"item")) {
      xmlNode *child_curr = curr->xmlChildrenNode;
      xmlChar *title;
      xmlChar *link;
      xmlChar *comments_link;
      while(child_curr != NULL) {
        if(!xmlStrcmp(child_curr->name, (const xmlChar *)"title")) {
          title = xmlNodeListGetString(doc, child_curr->xmlChildrenNode, 1);
        } else if(!xmlStrcmp(child_curr->name, (const xmlChar *)"link")) {
          link = xmlNodeListGetString(doc, child_curr->xmlChildrenNode, 1);
        } else if(!xmlStrcmp(child_curr->name, (const xmlChar *)"comments")) {
          comments_link = xmlNodeListGetString(doc, child_curr->xmlChildrenNode, 1);
        }

        child_curr = child_curr->next;
      }

      Post *post = malloc(sizeof(Post));
      post->title = malloc(strlen((char*)title) + 1);
      post->link = malloc(strlen((char*)link) + 1);
      post->comments_link = malloc(strlen((char*)comments_link) + 1);

      strcpy(post->title, (char*)title);
      strcpy(post->link, (char*)link);
      strcpy(post->comments_link, (char*)comments_link);

      if(title != NULL) {
        xmlFree(title);
        title = NULL;
      }
      if(link != NULL) {
        xmlFree(link);
        link = NULL;
      }
      if(comments_link != NULL) {
        xmlFree(comments_link);
        comments_link = NULL;
      }

      posts_arr[i] = post;

      ++i;
    }

    curr = curr->next;
  }

  xmlFreeDoc(doc);
  xmlCleanupParser();

  Posts *posts = malloc(sizeof(Posts));
  posts->arr = posts_arr;
  posts->len = num_items;

  return posts;
}

int main() {
  /*char *rss_xml = read_rss("https://hnrss.org/frontpage");*/
  /**/
  /*if(rss_xml == NULL) {*/
  /*  return 1;*/
  /*}*/
  /**/
  /*Posts *posts = parse_xml(rss_xml);*/
  /*for(unsigned int i = 0; i < posts->len; ++i) {*/
  /*  print_post((posts->arr)[i]);*/
  /*}*/
  /**/
  /*free(rss_xml);*/
  /*for(unsigned int i = 0; i < posts->len; ++i) {*/
  /*  free_post(posts->arr[i]);*/
  /*}*/
  /*free(posts->arr);*/
  /*free(posts);*/
  // system("firefox --new-window https://news.ycombinator.com");
  
  initscr();
  cbreak();
  noecho(); 

  printw("Hello, World!");
  refresh();

  int ch;
  while((ch = getch()) != 'q') {
    printw("%d", ch);
    refresh();
  }
  
  endwin();

  return 0;
}
