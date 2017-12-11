#include <iostream>
#include <string>
#include <vector>

#include <termios.h> 
#include <stropts.h>
//#include <fcntl.h>
#include <unistd.h>
#include <cctype>
#include <map>
#include <functional>


class Repl
{
	//{{{ Internal state

	std::string line;
	std::string prompt;
	int curpos;

	//{{{
	enum Mode
	{
		insert,
		normal
	} mode;
	//}}}
	//}}}

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

	//{{{ On keypress functions

	//{{{ Cursor Movement

	void move_cursor_begin(char)
	{
		curpos = 0;
	}

	void move_cursor_left(char)
	{
		if(curpos)
			curpos--;
	}

	void move_cursor_right(char)
	{
		if(curpos < static_cast<int>(line.length()))
			curpos++;
	}

	void move_cursor_end(char)
	{
		curpos = line.length();
	}

	void move_cursor_forward_word(char)
	{
		// TODO
	}

	void move_cursor_backward_word(char)
	{
		//TODO
	}
	//}}}

	//{{{ Mode Change

	void changemode_insert(char)
	{
		mode=insert;
	}

	void changemode_normal(char)
	{
		mode=normal;
	}
	//}}}

	//{{{ Insert/add character

	void add_char(char c)
	{
		if(curpos >= static_cast<int>(line.length())) line.append(1, c);
		else line.insert(curpos, 1, c);
		curpos++;
	}
	//}}}
	//}}}
	typedef void(Repl::*on_key_function)(char c);
	typedef char key;
	std::map<key, on_key_function> insert_mode_mappings;
	std::map<key, on_key_function> normal_mode_mappings;
	void run(std::map<key, on_key_function>& map, key c)
	{
		// If there is a mapping, execute the mapped function...
		if(on_key_function fun = map[c]; fun)
		{
			std::invoke(fun, *this, c);
		}
		else
		{
			// ... if not, add to the line in insert mode
			if(mode == Mode::insert) add_char(c);
		}
	}

	public:

	//{{{
	bool ins_char(char c)
	{
		switch(mode)
		{
			case Mode::insert: run(insert_mode_mappings, c); break;
			case Mode::normal: run(normal_mode_mappings, c); break;
			//default:
		}
		draw();
		return false;
	}
	//}}}

	//{{{
	void draw(void)
	{
		int cursor_position = prompt.length() + curpos;
		//std::cout<<cursor_position<<std::endl;
		std::cout<<"\33[2K\r"<<prompt<<line<<std::flush;
		std::cout<<"\33["<<cursor_position<<"G"<<std::flush;
	}
	//}}}

	//{{{
	Repl(const std::string& prompt = "edit > ") :
		line(""),
		prompt(prompt),
		curpos(0),
		mode(Mode::insert)
	{
		changemode(1);
		insert_mode_mappings['\33']=&Repl::changemode_normal;

		normal_mode_mappings['i']=&Repl::changemode_insert;
		normal_mode_mappings['h']=&Repl::move_cursor_left;
		normal_mode_mappings['l']=&Repl::move_cursor_right;
	}

	~Repl(void)
	{
		changemode(0);
	}
	//}}}
};
