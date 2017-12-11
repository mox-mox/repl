#include <iostream>
#include <string>
#include <vector>

#include <termios.h> 
#include <stropts.h>
//#include <fcntl.h>
#include <unistd.h>


class Repl
{
	//{{{
	void changemode(int dir)
	{
		//source: https://stackoverflow.com/a/13129698/4360539
		static struct termios oldt, newt;
		//static long   flags;

		if ( dir == 1 )
		{
			tcgetattr( STDIN_FILENO, &oldt);
			newt = oldt;
			newt.c_lflag &= ~( ICANON | ECHO );
			tcsetattr( STDIN_FILENO, TCSANOW, &newt);
			//flags = fcntl(STDIN_FILENO, F_GETFL);
			//fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
		}
		else
		{
			tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
			//fcntl(STDIN_FILENO, F_SETFL, flags);
		}
	}
	//}}}

	public:
		std::string line;
		std::string prompt;

		enum Mode
		{
			insert,
			normal
		} mode;

		std::string::iterator curpos;

		bool ins_char(char c)
		{
			line.append(1, c);
			return false;
		}

		void draw(void)
		{
			std::cout<<"\33[2K\r"<<prompt<<line<<std::flush;
			//std::cout<<"\33[4G"<<std::flush;
		}

		//{{{
		Repl(const std::string& prompt = "edit > ") : line(""), prompt(prompt), mode(insert), curpos(line.begin())
		{
			changemode(1);
		}

		~Repl(void)
		{
			changemode(0);
		}
		//}}}
};
