#include "ncurses.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <list>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CTRL(c) ((c)&037)

class String {
public:
  virtual ~String() {}
  virtual const char* data() const = 0;
  virtual char char_at(int i) const = 0;
  virtual int size() const = 0;
  virtual bool is_mutable() const = 0;
  virtual bool insert_char(char c, int i) = 0;
};

class ExternalString : public String {
public:
  ExternalString(const char* data, int size)
    : data_(data), size_(size) {}
  virtual const char* data() const { return data_; }
  virtual char char_at(int i) const { return data_[i]; }
  virtual int size() const { return size_; }
  virtual bool is_mutable() const { return false; }
  virtual bool insert_char(char, int) { /* Write terrible things here.*/ }
private:
  const char* data_;
  const int size_;
};

class AllocatedString : public String {
public:
  AllocatedString(const char* data, int size)
    : data_(data, size) {}
  virtual const char* data() const { return data_.c_str(); }
  virtual char char_at(int i) const { return data_[i]; }
  virtual int size() const { return data_.size(); }
  virtual bool is_mutable() const { return true; }
  virtual bool insert_char(char c, int i) {
    data_ = data_.substr(0, i) + c + data_.substr(i, data_.size() - i);
  }
private:
  std::string data_;
};

template<class Container>
void DeletePointers(Container* c) {
  for (typename Container::iterator it = c->begin(); it != c->end(); ++it) {
    delete *it;
  }
}

class PointerContainerDeleterImplBase {
public:
  virtual ~PointerContainerDeleterImplBase() {};
};

template<class Container>
class PointerContainerDeleterImpl : public PointerContainerDeleterImplBase {
public:
  PointerContainerDeleterImpl(Container* c) : c_(c) {}
  virtual ~PointerContainerDeleterImpl() { DeletePointers(c_); }
private:
  Container* c_;
};

class PointerContainerDeleter {
public:
  template<class Container> PointerContainerDeleter(Container* c)
    : impl_(new PointerContainerDeleterImpl<Container>(c)) {}
  ~PointerContainerDeleter() { delete impl_; }
private:
  PointerContainerDeleterImplBase* impl_;
};

int main(int argc, char** argv) {
  if (argc != 2) {
    printf("usage: mustang [filename to edit]\n");
    return 1;
  }

  struct stat st;
  if (stat(argv[1], &st) < 0) {
    printf("error stat failed %s\n", argv[1]);
    return 1;
  }

  if (!S_ISREG(st.st_mode)) {
    printf("%s is not a normal file\n", argv[1]);
    return 1;
  }

  int fd, flag;
  if ((fd = open(argv[1], flag = O_RDWR | O_CREAT)) < 0 &&
      (fd = open(argv[1], flag = O_RDONLY | O_CREAT)) < 0) {
    printf("error opening file %s\n", argv[1]);
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

  std::list<String*> lines;
  PointerContainerDeleter lines_deleter(&lines);
  char* p = file_content;
  for (int i = 0; i < line_num; ++i) {
    char* data = p;
    int size = 0;
    while (*p && *p != '\n') {
      ++p;
      ++size;
    }
    lines.push_back(new ExternalString(data, size));
    ++p;
  }

  initscr();
  noecho();
  scrollok(stdscr, TRUE);
  raw();  // Otherwise we can't handle CTRL-C.

  int height, width;
  getmaxyx(stdscr, height, width);
  std::list<String*>::iterator it = lines.begin();
  for (int i = 0; i < height; ++i) {
    if (it == lines.end()) break;
    for (int j = 0; j < width; ++j) {
      if (j == (*it)->size()) break;
      move(i, j);
      addch((*it)->char_at(j));
    }
    ++it;
  }

  move(0, 0);
  int buffer_y = 0;
  int buffer_x = 0;
  int cursor_x = 0;
  int cursor_y = 0;
  std::list<String*>::iterator current_line = lines.begin();
  bool modified = false;
  for (;;) {
    refresh();
    int c = getch();
    if (c == CTRL('x')) {
      c = getch();
      if (c == CTRL('c')) {
        break;
      } else if (c == CTRL('s')) {
        if (modified) {
          std::string result;
          for (std::list<String*>::iterator it = lines.begin();
               it != lines.end(); ++it) {
            result += std::string((*it)->data()) + std::string("\n");
          }
          int remaining = result.size();
          while (remaining) {
            int wrote = write(fd, result.c_str() + (result.size() - remaining),
                              remaining);
            if (wrote < 0) {
              break;
            }
            remaining -= wrote;
          }
          modified = false;
        }
      }
    } else if (c == CTRL('p')) {
      if (current_line == lines.begin()) continue;
      --current_line;
      --cursor_y;
      if (cursor_y == -1) {
        cursor_y = 0;
        if (buffer_y > 0) {
          --buffer_y;
          scrl(-1);
          for (int i = 0; i < (*current_line)->size(); ++i) {
            move(0, i);
            addch((*current_line)->char_at(i));
          }
        }
      }
      int max_x = current_line == lines.end() ? 0 :
        std::min(width, (*current_line)->size());
      cursor_x = std::min(cursor_x, max_x);
      move(cursor_y, cursor_x);
    } else if (c == CTRL('n')) {
      if (current_line == lines.end()) continue;
      ++current_line;
      ++cursor_y;
      if (cursor_y == height) {
        cursor_y = height - 1;
        if (buffer_y + height <= lines.size()) {
          ++buffer_y;
          scrl(1);
          if (current_line != lines.end()) {
            for (int i = 0; i < (*current_line)->size(); ++i) {
              move(height - 1, i);
              addch((*current_line)->char_at(i));
            }
          }
        }
      }
      int max_x = current_line == lines.end() ? 0 :
        std::min(width, (*current_line)->size());
      cursor_x = std::min(cursor_x, max_x);
      move(cursor_y, cursor_x);
    } else if (c == CTRL('b')) {
      --cursor_x;
      if (cursor_x == -1) {
        cursor_x = 0;
      }
      move(cursor_y, cursor_x);
    } else if (c == CTRL('f')) {
      ++cursor_x;
      int max_x = current_line == lines.end() ? 0 :
        std::min(width, (*current_line)->size());
      cursor_x = std::min(cursor_x, max_x);
      move(cursor_y, cursor_x);
    } else if (c == CTRL('e')) {
      cursor_x = current_line == lines.end() ? 0 :
        std::min(width, (*current_line)->size());
      move(cursor_y, cursor_x);
    } else if (c == CTRL('a')) {
      cursor_x = 0;
      move(cursor_y, cursor_x);
    } else {
      if (!(flag & (O_WRONLY | O_RDWR))) continue;
      if (current_line == lines.end()) {
        lines.push_back(new AllocatedString("", 0));
        --current_line;
      }
      if (!(*current_line)->is_mutable()) {
        *current_line = new AllocatedString((*current_line)->data(),
                                            (*current_line)->size());
      }
      (*current_line)->insert_char(c, cursor_x);
      modified = true;
      for (int i = 0; i < (*current_line)->size(); ++i) {
        move(cursor_y, i);
        addch((*current_line)->char_at(i));
      }
      ++cursor_x;
      move(cursor_y, cursor_x);
    }
  }

  move(0, 0);
  printw("Bye!");
  refresh();
  sleep(1);
  endwin();
  delete[] file_content;
  close(fd);

  return 0;
}
