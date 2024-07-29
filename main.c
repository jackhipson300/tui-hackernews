#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <assert.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <ncurses.h>

#define BOTTOM_MENU_HEIGHT 3
#define SIDE_MENU_WIDTH 30

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
  char *num_comments;
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
    free(posts->arr[i]->num_comments);
    free(posts->arr[i]);
  }
  free(posts->arr);
  free(posts);
}

Posts* parse_xml(const char *raw, regex_t *num_comments_regex) {
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
      xmlChar *description;
      xmlChar *link;
      xmlChar *comments_link;
      while(child_curr != NULL) {
        if(!xmlStrcmp(child_curr->name, (const xmlChar *)"title")) {
          title = xmlNodeListGetString(doc, child_curr->xmlChildrenNode, 1);
        } else if(!xmlStrcmp(child_curr->name, (const xmlChar *)"link")) {
          link = xmlNodeListGetString(doc, child_curr->xmlChildrenNode, 1);
        } else if(!xmlStrcmp(child_curr->name, (const xmlChar *)"comments")) {
          comments_link = xmlNodeListGetString(doc, child_curr->xmlChildrenNode, 1);
        } else if(!xmlStrcmp(child_curr->name, (const xmlChar *)"description")) {
          description = xmlNodeListGetString(doc, child_curr->xmlChildrenNode, 1);
        }

        child_curr = child_curr->next;
      }

      char *num_comments_str = "?";
      regmatch_t matches[2];
      int regexec_res = regexec(num_comments_regex, (char*)description, 2, matches, 0);
      if(regexec_res == 0) {
        size_t substr_size = (size_t)(matches[1].rm_eo - matches[1].rm_so);
        num_comments_str = malloc(sizeof(char) * (substr_size + 1));
        for(int j = matches[1].rm_so; j < matches[1].rm_eo; ++j) {
          num_comments_str[j - matches[1].rm_so] = (char)description[j];
        }
        num_comments_str[substr_size] = '\0';
      }

      Post *post = malloc(sizeof(Post));
      post->title = malloc(strlen((char*)title) + 1);
      post->link = malloc(strlen((char*)link) + 1);
      post->comments_link = malloc(strlen((char*)comments_link) + 1);
      post->num_comments = num_comments_str;

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
      if(description != NULL) {
        xmlFree(description);
        description = NULL;
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

void display_bottom_menu(WINDOW *win, int height, int width, char *choices[][2], int num_choices, char curr_filter) {
  box(win, 0, 0);

  int avail_width_per_item = width / num_choices;
  for(int i = 0; i < num_choices; ++i) {
    int len = (int)strlen(choices[i][1]);
    int avail_width = avail_width_per_item - (len + 2);
    int offset = avail_width / 2;
    if(offset < 0) {
      offset = 0;
    }

    int should_highlight = curr_filter == choices[i][0][0];
    int color_pair = 1;
    if(should_highlight) {
      color_pair = 3;
    }

    int absolute_offset = (avail_width_per_item * i) + offset;
    wmove(win, height / 2, absolute_offset);
    wattron(win, COLOR_PAIR(color_pair));
    wprintw(win, "%s", choices[i][0]);
    wattroff(win, COLOR_PAIR(color_pair));

    if(should_highlight) {
      wattron(win, A_REVERSE);
    }

    wmove(win, height / 2, absolute_offset + 1);
    wprintw(win, " ");
    wmove(win, height / 2, absolute_offset + 2);
    wprintw(win, "%s", choices[i][1]);

    if(curr_filter == choices[i][0][0]) {
      wattroff(win, A_REVERSE);
    }
  }

  wrefresh(win);
}

void display_side_menu(WINDOW *win, char *controls[][2], int num_controls) {
  box(win, 0, 0);

  int y ;
  for(int i = 0; i < num_controls; ++i) {
    y = (i * 2) + 1;
    wmove(win, y, 2);
    wattron(win, COLOR_PAIR(2));
    wprintw(win, "%s", controls[i][0]);
    wattroff(win, COLOR_PAIR(2));
    wmove(win, y, 4);
    wprintw(win, "%s", controls[i][1]);
  }

  wrefresh(win);
}

void display_posts(WINDOW *win, Posts *posts, int highlight_idx) {
  for(int i = 0; i < posts->len; ++i) {
    wmove(win, (i * 2) + 1, 2);
    wclrtoeol(win);
    if(highlight_idx == i) {
      wattron(win, A_REVERSE);
      wchgat(win, 5, A_REVERSE, 0, NULL);
    } else {
      wchgat(win, -1, A_NORMAL, 0, NULL);
    }

    wprintw(win, "%d.", i + 1);
    wmove(win, (i * 2) + 1, 6);
    wprintw(win, "%s", posts->arr[i]->title);
    wprintw(win, " (");
    wattron(win, COLOR_PAIR(1));
    wprintw(win, "%s", posts->arr[i]->num_comments);
    wattroff(win, COLOR_PAIR(1));
    wprintw(win, ")");

    if(highlight_idx == i) {
      wattroff(win, A_REVERSE);
    }
  }
  wrefresh(win);
}

Posts* get_posts(char *url, regex_t *num_comments_regex) {
  char *rss_xml = read_rss(url);
  
  if(rss_xml == NULL) {
    return NULL;
  }
  
  Posts* posts = parse_xml(rss_xml, num_comments_regex);
  free(rss_xml);
  return posts;
}

Posts* update_posts(Posts *posts, char *url, regex_t *num_comments_regex) {
  Posts *new_posts = get_posts(url, num_comments_regex);
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
  regex_t num_comments_regex;
  int regcomp_res = regcomp(&num_comments_regex, "# Comments: ([0-9]+)", REG_EXTENDED);
  assert(regcomp_res == 0);

  Posts *posts = get_posts(BEST_URL, &num_comments_regex);
  if(posts == NULL) {
    return 1;
  }

  char *choices[][2] = {
    { "b", "Best" },
    { "f", "Front Page" },
    { "n", "Newest" }
  };
  int num_choices = 3;

  char *controls[][2] = {
    { "j", "Move Down" },
    { "k", "Move Up" },
    { "o", "Open Article" },
    { "c", "Open Comments" },
    { "q", "Quit" }
  };
  int num_controls = 5;

  WINDOW *posts_win;
  WINDOW *bottom_menu_win;
  WINDOW *side_menu_win;
  
  initscr();
  start_color();
  cbreak();
  noecho(); 
  curs_set(0);

  init_pair(1, COLOR_GREEN, COLOR_BLACK);
  init_pair(2, COLOR_CYAN, COLOR_BLACK);
  init_pair(3, COLOR_GREEN, COLOR_WHITE);

  // initialize windows
  int screen_height, screen_width;
  getmaxyx(stdscr, screen_height, screen_width);
  posts_win = newwin(screen_height - BOTTOM_MENU_HEIGHT, screen_width - SIDE_MENU_WIDTH, 0, 0);
  bottom_menu_win = newwin(BOTTOM_MENU_HEIGHT, screen_width, screen_height - BOTTOM_MENU_HEIGHT, 0);
  side_menu_win = newwin(screen_height - BOTTOM_MENU_HEIGHT, SIDE_MENU_WIDTH, 0, screen_width - SIDE_MENU_WIDTH);
  refresh();

  int highlight_idx = 0;
  char curr_filter = 'b';

  // initial display
  display_side_menu(side_menu_win, controls, num_controls);
  display_bottom_menu(bottom_menu_win, BOTTOM_MENU_HEIGHT, screen_width, choices, num_choices, curr_filter);

  int should_refresh_bottom_menu;
  int ch;
  do {
    should_refresh_bottom_menu = 0;
    switch(ch) {
      case 'j':
        ++highlight_idx;
        break;
      case 'k':
        --highlight_idx;
        break;
      case 'b':
        if(curr_filter != 'b') {
          posts = update_posts(posts, BEST_URL, &num_comments_regex);
          curr_filter = 'b';
        }
        should_refresh_bottom_menu = 1;
        break;
      case 'f':
        if(curr_filter != 'f') {
          posts = update_posts(posts, FRONT_PAGE_URL, &num_comments_regex);
          curr_filter = 'f';
        }
        should_refresh_bottom_menu = 1;
        break;
      case 'n':
        if(curr_filter != 'n') {
          posts = update_posts(posts, NEWEST_URL, &num_comments_regex);
          curr_filter = 'n';
        }
        should_refresh_bottom_menu = 1;
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
      highlight_idx = posts->len - 1;
    }

    display_posts(posts_win, posts, highlight_idx);
    if(should_refresh_bottom_menu) {
      display_bottom_menu(bottom_menu_win, BOTTOM_MENU_HEIGHT, screen_width, choices, num_choices, curr_filter);
    }
    
    refresh();
  } while((ch = getch()) != 'q');
  
  delwin(posts_win);
  delwin(bottom_menu_win);
  delwin(side_menu_win);
  endwin();
  free_posts(posts);
  regfree(&num_comments_regex);

  return 0;
}
