#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <ncurses.h>

#define BOTTOM_MENU_HEIGHT 3
#define SIDE_MENU_WIDTH 30

#define MAX_NUM_POSTS 30

#define BEST_URL "https://hacker-news.firebaseio.com/v0/beststories.json"
#define FRONT_PAGE_URL "https://hacker-news.firebaseio.com/v0/topstories.json"
#define NEWEST_URL "https://hacker-news.firebaseio.com/v0/newstories.json"

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

char* curl_easy_request(const char *url) {
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
  int id;
  char *title;
  char *link;
  int score;
} Post;

void print_post(Post *post) {
  fprintf(stderr, "Id: %d\n", post->id);
  fprintf(stderr, "Title: %s\n", post->title);
  fprintf(stderr, "\tLink: %s\n", post->link);
  fprintf(stderr, "\tScore: %d\n", post->score);
}

void free_posts(Post** posts) {
  for(int i = 0; i < MAX_NUM_POSTS; ++i) {
    free(posts[i]->title);
    free(posts[i]->link);
    free(posts[i]);
  }
  free(posts);
}

int* get_post_ids(const char *raw) {
  cJSON *json = cJSON_Parse(raw);
  if(json == NULL) {
    fprintf(stderr, "JSON parse failed\n");
    return NULL;
  }

  if(!cJSON_IsArray(json)) {
    fprintf(stderr, "Post ids response is not array");
    return NULL;
  }

  int *arr = malloc(sizeof(int) * MAX_NUM_POSTS);
  cJSON *element;
  int i = 0;
  cJSON_ArrayForEach(element, json) {
    if(i == MAX_NUM_POSTS) {
      break;
    }

    arr[i] = element->valueint;
    ++i;
  }

  cJSON_Delete(json);

  return arr;
}

Post** get_posts(char *url) {
  char *raw = curl_easy_request(url);
  int *ids = get_post_ids(raw);

  CURL *handles[MAX_NUM_POSTS];
  struct MemoryStruct chunks[MAX_NUM_POSTS];
  CURLM *multi_handle = curl_multi_init();

  for(int i = 0; i < MAX_NUM_POSTS; ++i) {
    char post_url[100];
    snprintf(post_url, sizeof(post_url), "https://hacker-news.firebaseio.com/v0/item/%d.json", ids[i]);

    chunks[i].memory = malloc(1);
    chunks[i].size = 0;
    handles[i] = curl_easy_init();
    curl_easy_setopt(handles[i], CURLOPT_URL, post_url);
    curl_easy_setopt(handles[i], CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(handles[i], CURLOPT_WRITEDATA, (void*)&chunks[i]);

    curl_multi_add_handle(multi_handle, handles[i]);
  }

  int curl_running = 1;
  while(curl_running) {
    CURLMcode mcode = curl_multi_perform(multi_handle, &curl_running);
    if(curl_running) {
      mcode = curl_multi_poll(multi_handle, NULL, 0, 1000, NULL);
    }

    if(mcode) {
      break;
    }
  }

  CURLMsg *msg;
  int messages_left;
  while((msg = curl_multi_info_read(multi_handle, &messages_left)) != NULL) {
    if(msg->msg != CURLMSG_DONE) {
      fprintf(stderr, "Batched libcurl request not done when expected\n");
      return NULL;
    }
    if(msg->data.result != CURLE_OK) {
      fprintf(stderr, "Batched libcurl request error %s\n", curl_easy_strerror(msg->data.result));
      return NULL;
    }
  }

  int err = 0;
  Post **posts_arr = malloc(sizeof(Post*) * MAX_NUM_POSTS);
  for(int i = 0; i < MAX_NUM_POSTS; ++i) {
    cJSON *json = cJSON_Parse(chunks[i].memory);
    if(json == NULL) {
      fprintf(stderr, "JSON parse failed\n");
      return NULL;
    }

    cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "title");
    cJSON *link = cJSON_GetObjectItemCaseSensitive(json, "url");
    cJSON *score = cJSON_GetObjectItemCaseSensitive(json, "score");

    Post *post = malloc(sizeof(Post));
    post->id = ids[i];  

    if(!cJSON_IsString(title)) {
      fprintf(stderr, "Title is not string (%d)\n", ids[i]);
      err = 1;
    }

    if(!cJSON_IsString(link)) {
      // Ask HN and similar posts do not have an article link
      char hn_link[100];
      snprintf(hn_link, sizeof(hn_link), "https://news.ycombinator.com/item?id=%d", ids[i]);
      post->link = malloc(strlen(hn_link) + 1);
      strcpy(post->link, hn_link);     
    } else {
      post->link = malloc(strlen(link->valuestring) + 1);
      strcpy(post->link, link->valuestring);
    }

    if(!cJSON_IsNumber(score)) {
      fprintf(stderr, "Score is not number (%d)\n", ids[i]);
      err = 1;
    }

    if(err) {
      cJSON_Delete(json);
      break;
    }

    post->title = malloc(strlen(title->valuestring) + 1);
    post->score = score->valueint;

    strcpy(post->title, title->valuestring);

    posts_arr[i] = post;

    cJSON_Delete(json);
  }

  for(int i = 0; i < MAX_NUM_POSTS; ++i) {
    free(chunks[i].memory);
    curl_multi_remove_handle(multi_handle, handles[i]);
    curl_easy_cleanup(handles[i]);
  }
  curl_multi_cleanup(multi_handle);
  free(raw);
  free(ids);

  if(err) {
    return NULL;
  }

  return posts_arr;
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

void display_posts(WINDOW *win, Post **posts, int highlight_idx) {
  for(int i = 0; i < MAX_NUM_POSTS; ++i) {
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
    wprintw(win, "%s", posts[i]->title);
    wprintw(win, " (");
    wattron(win, COLOR_PAIR(1));
    wprintw(win, "%d", posts[i]->score);
    wattroff(win, COLOR_PAIR(1));
    wprintw(win, ")");

    if(highlight_idx == i) {
      wattroff(win, A_REVERSE);
    }
  }
  wrefresh(win);
}

void update_posts(Post ***posts, char *url) {
  Post **new_posts = get_posts(url);
  if(new_posts == NULL) {
    fprintf(stderr, "Error updating posts: posts are null");
    return;
  }

  free_posts(*posts);
  *posts = new_posts;
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
  if(freopen("debug.txt", "w", stderr) == NULL) {
    fprintf(stderr, "Error writing debug log");
    return 1;
  }

  Post **posts = get_posts(FRONT_PAGE_URL);
  if(posts == NULL) {
    fprintf(stderr, "Posts are null");
    return 1;
  }

  char *choices[][2] = {
    { "f", "Front Page" },
    { "b", "Best" },
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
  char curr_filter = 'f';

  // initial display
  display_side_menu(side_menu_win, controls, num_controls);
  display_bottom_menu(bottom_menu_win, BOTTOM_MENU_HEIGHT, screen_width, choices, num_choices, curr_filter);
  display_posts(posts_win, posts, highlight_idx);

  int should_refresh_bottom_menu;
  int ch;
  while((ch = getch()) != 'q') {
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
          update_posts(&posts, BEST_URL);
          curr_filter = 'b';
        }
        should_refresh_bottom_menu = 1;
        break;
      case 'f':
        if(curr_filter != 'f') {
          update_posts(&posts, FRONT_PAGE_URL);
          curr_filter = 'f';
        }
        should_refresh_bottom_menu = 1;
        break;
      case 'n':
        if(curr_filter != 'n') {
          update_posts(&posts, NEWEST_URL);
          curr_filter = 'n';
        }
        should_refresh_bottom_menu = 1;
        break;
      case 'o':
        open_link(posts[highlight_idx]->link);
        break;
      case 'c':
        // TODO open_link(posts[highlight_idx]->comments_link);
        break;
      default:
        break;
    }

    if(highlight_idx < 0) {
      highlight_idx = 0;
    } else if(highlight_idx >= MAX_NUM_POSTS) {
      highlight_idx = MAX_NUM_POSTS - 1;
    }

    display_posts(posts_win, posts, highlight_idx);
    if(should_refresh_bottom_menu) {
      display_bottom_menu(bottom_menu_win, BOTTOM_MENU_HEIGHT, screen_width, choices, num_choices, curr_filter);
    }
    
    refresh();
  } 
  
  delwin(posts_win);
  delwin(bottom_menu_win);
  delwin(side_menu_win);
  endwin();
  free_posts(posts);

  return 0;
}
