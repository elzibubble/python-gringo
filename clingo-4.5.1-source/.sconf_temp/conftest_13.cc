#include <cstdio>
#include <cstdarg>
int main() {
char buf[256]; va_list args; vsnprintf (buf,256,"", args);
return 0; }