#include <stdio.h>
#include <string.h>

int rpmvercmp(const char *a, const char *b);

int main(int argc, char *argv[])
{
	char s1[255] = "";
	char s2[255] = "";
	int ret;

	if(argc > 1) {
		strncpy(s1, argv[1], 255);
	}
	if(argc > 2) {
		strncpy(s2, argv[2], 255);
	} else {
		printf("0\n");
		return(0);
	}
	
	ret = rpmvercmp(s1, s2);
	printf("%d\n", ret);
	return(ret);
}
