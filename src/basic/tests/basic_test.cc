#include "basic.h"
#include <string>

using namespace std;

int main () {
  int a = 5;
  string s = "hello";
  double d = 3.14;

  cout << LOGVARS(a, s, d) << endl;
}
