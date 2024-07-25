#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <ncurses.h>

#define MENU_HEIGHT 3
#define BEST_URL "https://hnrss.org/best"
#define FRONT_PAGE_URL "https://hnrss.org/frontpage"
#define NEWEST_URL "https://hnrss.org/newest"

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
  int len;
} Posts;

void print_post(Post *post) {
  printf("Title: %s\n", post->title);
  printf("\tLink: %s\n", post->link);
  printf("\tComments: %s\n", post->comments_link);
}

void free_posts(Posts *posts) {
  for(int i = 0; i < posts->len; ++i) {
    free(posts->arr[i]->title);
    free(posts->arr[i]->link);
    free(posts->arr[i]->comments_link);
    free(posts->arr[i]);
  }
  free(posts->arr);
  free(posts);
}

Posts* parse_xml(const char *raw) {
  xmlDoc *doc = NULL;

  doc = xmlReadMemory(raw, (int)strlen(raw), NULL, NULL, 0);
  if(doc == NULL) {
    fprintf(stderr, "XML parse failed\n");
    return NULL;
  }

  int num_items = 0;
  xmlNode *curr = xmlDocGetRootElement(doc);
  curr = curr->xmlChildrenNode->xmlChildrenNode;
  while(curr != NULL) {
    if(!xmlStrcmp(curr->name, (const xmlChar *)"item")) {
      ++num_items;
    }

    curr = curr->next;
  }

  Post **posts_arr = malloc((size_t)((int)sizeof(Post*) * num_items));

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

void print_menu(WINDOW *win, int height, int width, char *choices[][2], int num_choices) {
  box(win, 0, 0);
  int avail_width_per_item = width / num_choices;

  for(int i = 0; i < num_choices; ++i) {
    int len = (int)strlen(choices[i][1]);
    int avail_width = avail_width_per_item - (len + 2);
    int offset = avail_width / 2;
    if(offset < 0) {
      offset = 0;
    }

    int absolute_offset = (avail_width_per_item * i) + offset;
    wmove(win, height / 2, absolute_offset);
    wattron(win, COLOR_PAIR(1));
    wprintw(win, "%s", choices[i][0]);
    wattroff(win, COLOR_PAIR(1));
    wmove(win, height / 2, absolute_offset + 2);
    wprintw(win, "%s", choices[i][1]);
  }
  wrefresh(win);
}

void print_posts(WINDOW *win, Posts *posts, int highlight_idx) {
  for(int i = 0; i < posts->len; ++i) {
    wmove(win, i * 2, 0);
    wclrtoeol(win);
    if(highlight_idx == i) {
      wattron(win, A_REVERSE);
      wchgat(win, 5, A_REVERSE, 0, NULL);
    } else {
      wchgat(win, -1, A_NORMAL, 0, NULL);
    }

    wprintw(win, "%d.", i + 1);
    wmove(win, i * 2, 4);
    wprintw(win, "%s ", posts->arr[i]->title);

    if(highlight_idx == i) {
      wattroff(win, A_REVERSE);
    }
  }
  wrefresh(win);
}

Posts* get_posts(char *url) {
  char *rss_xml = read_rss(url);
  
  if(rss_xml == NULL) {
    return NULL;
  }
  
  Posts* posts = parse_xml(rss_xml);
  free(rss_xml);
  return posts;
}

Posts* update_posts(Posts *posts, char *url) {
  Posts *new_posts = get_posts(url);
  if(new_posts == NULL) {
    return posts;
  }

  free_posts(posts);
  return new_posts;
}

void open_link(char *link) {
  pid_t pid = fork();
  if(pid == 0) {
    // redirect output from child process since it messes with curses
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);

    execlp("firefox", "firefox", "--new-window", link, (char*)NULL);
  }
}

int main() {
  Posts *posts = get_posts(BEST_URL);
  if(posts == NULL) {
    return 1;
  }

  char *choices[][2] = {
    { "b", "Best" },
    { "f", "Front Page" },
    { "n", "Newest" }
  };
  int num_choices = 3;

  WINDOW *menu_win;
  
  initscr();
  start_color();
  cbreak();
  noecho(); 

  init_pair(1, COLOR_GREEN, COLOR_BLACK);

  int screen_height, screen_width;
  getmaxyx(stdscr, screen_height, screen_width);

  menu_win = newwin(MENU_HEIGHT, screen_width, screen_height - MENU_HEIGHT, 0);
  refresh();

  int ch;
  int highlight_idx = 0;
  char curr_filter = 'b';
  do {
    switch(ch) {
      case 'j':
        ++highlight_idx;
        break;
      case 'k':
        --highlight_idx;
        break;
      case 'b':
        if(curr_filter != 'b') {
          posts = update_posts(posts, BEST_URL);
          curr_filter = 'b';
        }
        break;
      case 'f':
        if(curr_filter != 'f') {
          posts = update_posts(posts, FRONT_PAGE_URL);
          curr_filter = 'f';
        }
        break;
      case 'n':
        if(curr_filter != 'n') {
          posts = update_posts(posts, NEWEST_URL);
          curr_filter = 'n';
        }
        break;
      case 'o':
        open_link(posts->arr[highlight_idx]->link);
        break;
      case 'c':
        open_link(posts->arr[highlight_idx]->comments_link);
        break;
      default:
        break;
    }

    if(highlight_idx < 0) {
      highlight_idx = 0;
    } else if(highlight_idx >= posts->len) {
      highlight_idx = posts->len;
    }

    print_posts(stdscr, posts, highlight_idx);
    print_menu(menu_win, MENU_HEIGHT, screen_width, choices, num_choices);
    refresh();
  } while((ch = getch()) != 'q');
  
  endwin();
  free_posts(posts);

  return 0;
}
