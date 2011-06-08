#include "ncurses.h"
#include "signal.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>

#define CTRL(c) ((c)&037)

struct Line {
  struct Line* next;
  struct Line* prev;
  char* line;
  int len;
};

void signal_handler(int sig) {
  move(0, 0);
  printw("Bye!");
  refresh();
  sleep(1);
  endwin();
  exit(0);
}

int main(int argc, char** argv) {
  signal(SIGINT, signal_handler);

  if (argc != 2) {
    printf("usage: mustang [filename to edit]\n");
    return 1;
  }

  int fd = open(argv[1], O_RDWR | O_CREAT);
  if (fd == -1) {
    printf("error opening file %s", argv[1]);
    return 1;
  }

  lseek(fd, 0, SEEK_SET);
  int file_size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  char* file_content = new char[file_size];
  int r = read(fd, file_content, file_size);
  if (r != file_size) {
    printf("%d %d\n", file_size, r);
    printf("read error.\n");
    return 1;
  }

  int line_num = 0;
  for (char* p = file_content; *p; ++p) {
    if (*p == '\n') {
      ++line_num;
    }
  }

  Line* lines = new Line[line_num];
  char* p = file_content;
  for (int i = 0; i < line_num; ++i) {
    lines[i].next = (i + 1 < line_num) ? &lines[i + 1] : NULL;
    lines[i].prev = (0 <= i - 1) ? &lines[i - 1] : NULL;
    lines[i].line = p;
    lines[i].len = 0;
    while (*p && *p != '\n') {
      ++p;
      ++lines[i].len;
    }
    ++p;
  }

  initscr();
  noecho();
  scrollok(stdscr, TRUE);

  int buffer_y = 0;
  int buffer_x = 0;

  refresh();
  int height, width;
  getmaxyx(stdscr, height, width);
  for (int i = 0; buffer_y + i < line_num && i < height; ++i) {
    for (int j = 0; buffer_x + j < lines[i].len && j < width; ++j) {
      move(i, j);
      addch(lines[i].line[j]);
    }
  }

  move(0, 0);
  int cursor_x = 0;
  int cursor_y = 0;
  for (;;) {
    refresh();
    int c = getch();
    if (c == CTRL('p')) {
      --cursor_y;
      if (cursor_y == -1) {
        cursor_y = 0;
        if (buffer_y > 0) {
          --buffer_y;
          scrl(-1);
          for (int i = 0; i < lines[buffer_y].len; ++i) {
            move(0, i);
            addch(lines[buffer_y].line[i]);
          }
        }
      }
      move(cursor_y, cursor_x);
    } else if (c == CTRL('n')) {
      ++cursor_y;
      if (cursor_y == height) {
        cursor_y = height - 1;
        if (buffer_y + height < line_num) {
          ++buffer_y;
          scrl(1);
          if (buffer_y + height <= line_num) {
            for (int i = 0; i < lines[buffer_y + height - 1].len; ++i) {
              move(height - 1, i);
              addch(lines[buffer_y + height - 1].line[i]);
            }
          }
        }
      }
      move(cursor_y, cursor_x);
    } else if (c == CTRL('b')) {
      --cursor_x;
      if (cursor_x == -1) {
        cursor_x = 0;
      }
      move(cursor_y, cursor_x);
    } else if (c == CTRL('f')) {
      ++cursor_x;
      int max_x = std::min(width, lines[buffer_y + cursor_y].len);
      if (cursor_x > max_x) {
        cursor_x = max_x;
      }
      move(cursor_y, cursor_x);
    } else if (c == CTRL('e')) {
      cursor_x = std::min(width, lines[buffer_y + cursor_y].len);
      move(cursor_y, cursor_x);
    } else if (c == CTRL('a')) {
      cursor_x = 0;
      move(cursor_y, cursor_x);
    } else {
      addch(c);
    }
  }
  // UNREACHABLE
  endwin();
  delete[] file_content;
  delete[] lines;
  close(fd);
  return 0;
}
