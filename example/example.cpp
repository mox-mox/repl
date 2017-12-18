#include "repl.hpp"

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
	repl.default_mappings();
	repl.change_terminal_mode(1);
	repl.draw();

	std::array<char, 5> buf;
	while(true)
	{
		if(!kbhit()) continue;
		int n = read(STDIN_FILENO, buf.data(), buf.max_size());
		for(std::string& line : repl.insert(buf.data(), n))
		{
			if(line == KILL_PILL) goto finish;
			std::cout<<"\nGot line "<<line<<std::endl;
		}
		repl.draw();
	}
finish:
	repl.change_terminal_mode(0);
	return 0;
}
