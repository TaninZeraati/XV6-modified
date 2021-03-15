#include "types.h"
#include "user.h"
#include "fcntl.h"


#include <stdbool.h>


void save(int result[])
{

  static char digits[] = "01";
  char buf[100];
  int fd;

  fd = open("strdiff_result.txt", O_CREATE | O_RDWR);
  int i;
  for(i = 0; i < sizeof(*result)*2; i++){
    buf[i] = digits[result[i]];
    write(fd, &buf[i], 1);
  }

}

void strdiff(char* first_str, char* second_str){

  int size_str;
  bool second;
  int result[100];
  if (sizeof(first_str) < sizeof(second_str)){

    size_str = strlen(first_str);
    second = false;
  }
  else{
    size_str = strlen(second_str);
    second = true;
  }
  for(int i = 0; i < size_str; i++){
    if((first_str[i]-'0') >= (second_str[i]-'0')){
      result[i] = 0;
    }
    else{
      result[i] = 1;
    }
  }
  if(second){
    for(int j = size_str; j < sizeof(second_str); j++){
      result[j] = 1;
    }
  }
  else{
      for(int j = size_str; j < sizeof(first_str); j++){
        result[j] = 0;
    }
  }
  save(result);

}

int main(int argc, char *argv[])
{

    strdiff(argv[1], argv[2]);
    exit();
}
