#include "repl.hpp"
#include <array>

//{{{
int kbhit (void) 
{
	//source: https://stackoverflow.com/a/13129698/4360539
	struct timeval tv;
	fd_set rdfs;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&rdfs);
	FD_SET (STDIN_FILENO, &rdfs);

	select(STDIN_FILENO+1, &rdfs, NULL, NULL, &tv);
	return FD_ISSET(STDIN_FILENO, &rdfs);
}
//}}}


int main(void)
{
	std::cout<<"Repl!"<<std::endl;

	Repl repl;
	repl.draw();

	std::array<char, 5> buf;
	while(true)
	{
		if(!kbhit()) continue;
		int n = read(STDIN_FILENO, buf.data(), buf.max_size());
		for(int i=0; i<n; i++)
		{
			repl.ins_char(buf[i]);
		}
		repl.draw();
	}
	return 0;
}
