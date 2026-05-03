//
// expend file size to dword alignment by appending zero to end of file
//

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	FILE *filein;
	long i, size;
	char c = 0;

	if (argc != 2) 
	{
		printf("Usage: sizetodword filename\n");
		exit(1);
	}

	if ((filein = fopen(argv[1], "ab")) == NULL)
	{
		printf("failed to open file %s\n", argv[1]);
		exit(2);
	}	

	size = ftell(filein);

	if (size % sizeof(long))
	{
		for (i = 0; i < sizeof(long) - size % sizeof(long); i++)
		{
			fwrite(&c, 1, 1, filein);
		}
	}
	
	fclose(filein);

	return 0;
}
