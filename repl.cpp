#include "repl.hpp"

//{{{ Constructors

Repl::Repl(const std::string& prompt, milliseconds escape_sequence_timeout) :
	escape_sequence_timeout(escape_sequence_timeout),
	prompt(prompt),
	line(""),
	curpos(*this,0),
	accepted_lines(),
	history(),
	mode(Mode::INSERT)
{
	//change_terminal_mode(1);
	//draw();
}

Repl::~Repl(void)
{
	//change_terminal_mode(0);
}
//}}}


//{{{ int Repl::insert(char buf[], std::size_t len)
//Handle user input

// Handle user input one key at a time
// Depending on mode (NORMAL/INSERT) key mappings are checked and corresponding functions are run.
// It may be that the input is not a key the user pressed but an escape sequence sent by the terminal,
// and there are two ways to check this. They both use the fact, that a user is slow, while the terminal
// is fast an can send multiple 'keys' at once.
// Option 1) When an ESC is entered, start a timer and store the key in a special buffer. If there is
// input before the timer runs out (and that input is "[..."), the input must have been an escape sequence
// and should be treated as such. If there was no follow-up input, it will have been a literal ESC pressed by the user.
// Option 2) Look at multiple keys at once: If the terminal is configured to send keys immediately, we should normally only receive single keys, but if we receive an escape sequence, we will receive multiple keys at once and so should be able to just check if the next key is '['.

int Repl::insert(char buf[], std::size_t len)
{
	static std::string escape_sequence_buffer;
	steady_clock::time_point sequence_start;
	for(std::size_t i=0; i<len; i++)
	{
		//std::cout<<int(buf[i])<<std::endl;
		if(buf[i] == EOT) // Received <C-D>
		{
			return -1;
		}

		//{{{
		if(mode != INSERT_ESCAPED && mode != NORMAL_ESCAPED) // If we are not in an escape sequence
		{
			switch(buf[i])
			{
				// Other special caases
				case ESC:
					// see if we can get the next character
					if(i+1 >= len) // If we cannot get the next character we'll have to set a timeout to know when/if it arrives
					{
						escape_sequence_buffer+=ESC;
						sequence_start=steady_clock::now();
						if(mode==INSERT) mode=INSERT_ESCAPED;
						if(mode==NORMAL) mode=NORMAL_ESCAPED;
						// Start a timeout: When it completes, it will call insert(ESC, 1)
						// This way, we know that this was a real key press
						// TODO: start the timer
						break;
					}
					else //if(i+1 < len)
					{
						if(buf[i+1] == '[') // This IS the start of an escape sequence
						{
							i++; // we consume the (i+1)th key as well so hop over it in the next iteration.
							escape_sequence_buffer.append("\33[");
							sequence_start=steady_clock::now();
							if(mode==INSERT) mode=INSERT_ESCAPED;
							if(mode==NORMAL) mode=NORMAL_ESCAPED;
						}
						//else // This is NOT the start of an escape sequence so process the key press (using the fallthrough)
					}
					[[fallthrough]];
				default:
					insert_key(buf[i]);
			}
		}
		//}}}
		//{{{
		else // mode == INSERT_ESCAPED || mode==NORMAL_ESCAPED means we are possibly in an escape sequence
		{
			if(escape_sequence_buffer.length() == 1)
			{
				// If the timer is running, stop it
				// TODO: Stop the timer

				// Escape sequences begin with "ESC[" and the '[' should arrive faster than the user can type
				if( (buf[i] == '[') && (std::chrono::duration_cast<milliseconds>(sequence_start-steady_clock::now()) < escape_sequence_timeout))
				{
					// This is an escape sequence, the next characters determine what action should be taken
					escape_sequence_buffer+=buf[i];
				}
				else
				{
					// This is not an escape sequence, so treat the stored 'ESC' and buf[i] as keys
					if(mode==INSERT_ESCAPED) mode=INSERT;
					if(mode==NORMAL_ESCAPED) mode=NORMAL;
					insert_key(ESC);
					escape_sequence_buffer.clear();
					if(buf[i] != ESC) // If we were called by the timer, the second key is inserted by the timer and should be ignored
					{
						insert_key(buf[i]);
					}
				}
			}
			else
			{
				// This is an escape sequence, add the new character and try to run it.
				escape_sequence_buffer+=buf[i];
				if(insert_key(escape_sequence_buffer))
				{
					// The escape sequence was recognised, so we are not in an escape sequence any more.
					escape_sequence_buffer.clear();
					if(mode==INSERT_ESCAPED) mode=INSERT;
					if(mode==NORMAL_ESCAPED) mode=NORMAL;
				}
			}
		}
		//}}}
	}
	draw();
	return accepted_lines.size();
}
//}}}


//{{{ Insert one key press or escape sequence

void Repl::insert_key(Key key)
{
	insert_key(std::string(1, key));
}
bool Repl::insert_key(std::string key)
{
	static on_keypress_function* add_char=&actions.find("add_char")->second;

	// If there is a mapping, execute the mapped function...
	if(auto fun = mappings[mode].find(key); fun != mappings[mode].end())
	{
		std::invoke(fun->second, key[0]);
		return true;
	}
	else if(mode==INSERT && std::isprint(key[0])) // ... if not, add to the line if in insert mode
	{
		std::invoke(*add_char, key[0]);
	}
	else if(mode==INSERT_ESCAPED || mode==NORMAL_ESCAPED) // ... if not, print some debugging information
	{
		key.erase(0, 1);
		std::cerr<<"Unknown escape sequence ESC"<<key<<std::endl;
		if(key.length() > 20)
		{
			std::cerr<<"Accepting (and ignoring) really long unknown escape sequence to allow recovery"<<std::endl;
			return true;
		}
	}
	return false;
}
//}}}


//{{{
void Repl::draw(void)
{
	//std::cout<<std::endl<<static_cast<int>(curpos)<<std::endl;
	int cursor_position = 1+prompt.length() + static_cast<int>(curpos);
	std::cout<<"\33[2K\r"<<prompt<<line;//<<std::flush;
	std::cout<<"\33["<<cursor_position<<"G"<<std::flush;
}
//}}}


//{{{
void Repl::map(Mode mode, Key key_combo, std::string action)
{
	map(mode, std::string(1,key_combo), action);
}

void Repl::map(Mode mode, std::string key_combo, std::string action)
{
	// Escape sequences are passed as 'E''S''C''['... , turn this into 'ESC''['... (\33[...)
	if(key_combo.length() >= 3 && key_combo.find("ESC")==0)
	{
		key_combo.erase(0, 2);
		key_combo[0] = ESC;
	}
	if(auto fun = actions.find(action); fun != actions.end())
	{
		mappings[mode][key_combo] = fun->second;
	}
	else
	{
		if(key_combo[0] != ESC)
		{
			std::cerr<<"Cannot map key combo \""<<key_combo<<"\" --> action \""<<action<<"\": Action unknown."<<std::endl;
		}
		else
		{
			key_combo.erase(0, 1);
			std::cerr<<"Cannot map escape sequece \"ESC"<<key_combo<<"\" --> action \""<<action<<"\": Action unknown."<<std::endl;
		}
	}
}
//}}}


//{{{
void Repl::default_mappings(void)
{
	// mappings for insert mode
	map(INSERT, ESC, "changemode_normal");
	map(INSERT, DEL, "backspace");
	map(INSERT, '\r', "accept");
	map(INSERT, ETX, "kill_line");

	// mappings for normal mode
	map(NORMAL, 'I', "changemode_insert_begin");
	map(NORMAL, 'i', "changemode_insert");
	map(NORMAL, 'a', "changemode_append");
	map(NORMAL, 'A', "changemode_append_end");
	map(NORMAL, 'x', "delete_char");
	map(NORMAL, 'X', "backspace");
	map(NORMAL, '\r', "accept");
	map(NORMAL, ETX, "kill_line");

	map(NORMAL, '0', "move_cursor_begin");
	map(NORMAL, 'b', "move_cursor_word_begin");
	map(NORMAL, 'h', "move_cursor_left");
	map(NORMAL, 'l', "move_cursor_right");
	map(NORMAL, 'e', "move_cursor_word_end");
	map(NORMAL, 'w', "move_cursor_word_next");
	map(NORMAL, '$', "move_cursor_end");
}
//}}}


//{{{
std::vector<std::string>& Repl::get_accepted_lines(void)
{
	return accepted_lines;
}
//}}}


//{{{
void Repl::change_terminal_mode(int dir)
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

