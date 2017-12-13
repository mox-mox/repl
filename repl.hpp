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
#include <cctype>
#include <chrono>
#include <memory>

#define ESC '\33'

class Repl
{
	//{{{ Internal state
	using milliseconds = std::chrono::duration<int64_t, std::milli>;
	using steady_clock = std::chrono::steady_clock;
	milliseconds escape_sequence_timeout;

	std::string prompt;
	std::string line;
	int curpos;

	std::vector<std::string> accepted_lines;
	std::vector<std::string> history;

	//{{{
	enum Mode
	{
		INSERT,
		NORMAL
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

	//{{{
	inline void check_curpos(void)
	{
		if(curpos < 0) curpos=0;
		if(static_cast<int>(line.length()) < curpos) curpos = line.length();
	}
	//}}}

	//{{{ On keypress functions

	// Yes, this is a huge std::map of lambdas
	std::map<std::string, std::function<bool(char)>> keypress_functions = { // TODO: Replace with normal functions when reflection becomes available in C++
		//{{{ Cursor movement

		{ "move_cursor_begin",        [this](char c){ curpos=0; return false; }},
		{ "move_cursor_left",         [this](char c){ if(curpos) curpos--; return false; }},
		{ "move_cursor_right",        [this](char c){ if(curpos < static_cast<int>(line.length()) curpos++; return false; }},
		{ "move_cursor_end",          [this](char c){ curpos=line.length(); return false; }},
		{ "move_cursor_forward_word", [this](char c){ for(; curpos<line.length()&&line[curpos]!=' '; curpos++); return false; }},
		{ "move_cursor_backward_word",[this](char c){ for(; curpos&&line[curpos]!=' '; curpos--); return false; }},
		//}}}
		//{{{ Mode change

		{ "changemode_insert",        [this](char c){ mode=INSERT; return false; }},
		{ "changemode_append",        [this](char c){ curpos++; mode=INSERT; return false; }}, // TODO: Check
		{ "changemode_normal",        [this](char c){ mode=NORMAL; return false; }},
		//}}}
		//{{{ Insert/delete character

		{ "add_char",                 [this](char c){ line.insert(curpos,1,c); curpos++; return false; }},
		{ "delete_char",              [this](char c){ line.erase(curpos,1; curpos++; return false; }},
		//}}}
		//{{{ Accept line

		{ "accept",                   [this](char c){ curpos=0; history.push_back(line); accepted_lines.push_back(line); line.clear(); return true; }},
		{ "accept_no_add_history",    [this](char c){ curpos=0; accepted_lines.push_back(line); line.clear(); return true; }}
		//}}}
		////{{{ Complete

		//{ "complete_single_word",     [this](char c){ if(word_completer)    std::invoke(word_completer(   line, curpos); check_curpos(); return false; }},
		//{ "complete_multi_word",      [this](char c){ if(multi_completer)   std::invoke(multi_completer(  line, curpos); check_curpos(); return false; }},
		//{ "complete_history",         [this](char c){ if(history_completer) std::invoke(history_completer(line, curpos); check_curpos(); return false; }},
		////}}}
		////{{{ History search

		//{ "search_global_hist_fwd",   [this](char c){ if(global_hist_fwd)   std::invoke(global_hist_fwd(  line, curpos); check_curpos(); return false; }},
		//{ "search_global_hist_bwd",   [this](char c){ if(global_hist_fwd)   std::invoke(global_hist_fwd(  line, curpos); check_curpos(); return false; }},
		//{ "search_matching_hist_fwd", [this](char c){ if(matching_hist_fwd) std::invoke(matching_hist_fwd(line, curpos); check_curpos(); return false; }},
		//{ "search_matching_hist_bwd", [this](char c){ if(matching_hist_bwd) std::invoke(matching_hist_bwd(line, curpos); check_curpos(); return false; }},
		////}}}
	};
	//}}}





	typedef bool(Repl::*on_key_function)(char c);
	typedef char key;
	std::map<key, on_key_function>                insert_mode_mappings; // Mappings in insert mode (default mode)
	std::map<key, on_key_function>                normal_mode_mappings; // Mappings in normal mode (usually after pressing ESC)
	std::map<std::string, on_key_function> escape_insert_mode_mappings; // Mappings for escape sequences (sent by the terminal)
	std::map<std::string, on_key_function> escape_normal_mode_mappings; // Mappings for escape sequences (sent by the terminal)
	//{{{
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
			if(mode == Mode::INSERT && std::isprint(c))
			{
				add_char(c);
			}
		}
	}
	bool run(std::map<std::string, on_key_function>& map, std::string c)
	{
		// If there is a mapping, execute the mapped function...
		if(on_key_function fun = map[c]; fun)
		{
			std::invoke(fun, *this, 'x');
			return true;
		}
		else
		{
			// ... if not, print some debugging information
			c.erase(0, 1);
			std::cerr<<"Unknown escape sequence ESC"<<c<<std::endl;
		}
		return false;
	}
	//}}}

	public:

	//{{{ Insert one key press

	void insert_key(char c)
	{
		//std::cout<<int(c)<<std::endl;

		switch(mode)
		{
			case Mode::INSERT: run(insert_mode_mappings, c); break;
			case Mode::NORMAL: run(normal_mode_mappings, c); break;
			default: std::cerr<<"Unknown mode "<<mode<<std::endl;
		}
	}
	//}}}

	//{{{ Insert one escape sequence

	bool insert_esc_seq(const std::string& c)
	{
		//std::cout<<c<<std::endl;
		switch(mode)
		{
			case Mode::INSERT: run(escape_insert_mode_mappings, c); return true; break;
			case Mode::NORMAL: run(escape_normal_mode_mappings, c);  return true;break;
			default: std::cerr<<"Unknown mode "<<mode<<std::endl; return false;
		}
	}
	//}}}


	public:
	//{{{
	void draw(void)
	{
		int cursor_position = prompt.length() + curpos;
		std::cout<<"\33[2K\r"<<prompt<<line<<std::flush;
		std::cout<<"\33["<<cursor_position<<"G"<<std::flush;
	}
	//}}}

	////{{{
	//void map(std::string key_combo, std::string action)
	//{

	//}
	////}}}

	//{{{ Handle user input

	// Handle user input one key at a time
	// Depending on mode (NORMAL/INSERT) key mappings are checked and corresponding functions are run.
	// It may be that the input is not a key the user pressed but an escape sequence sent by the terminal,
	// and there are two ways to check this. They both use the fact, that a user is slow, while the terminal
	// is fast an can send multiple 'keys' at once.
	// Option 1) When an ESC is entered, start a timer and store the key in a special buffer. If there is
	// input before the timer runs out (and that input is "[..."), the input must have been an escape sequence
	// and should be treated as such. If there was no follow-up input, it will have been a literal ESC pressed by the user.
	// Option 2) Look at multiple keys at once: If the terminal is configured to send keys immediately, we should normally only receive single keys, but if we receive an escape sequence, we will receive multiple keys at once and so should be able to just check if the next key is '['.

	//int insert(std::unique_ptr<char[]> buf, std::size_t len)
	int insert(char buf[], std::size_t len)
	{
		static std::string escape_sequence_buffer;
		steady_clock::time_point sequence_start;
		for(std::size_t i=0; i<len; i++)
		{
			//std::cout<<int(buf[i])<<std::endl;
			if(!escape_sequence_buffer.length()) // If we are not in an escape sequence
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
							}
							//else // This is NOT the start of an escape sequence so process the key press (using the fallthrough)
						}
						[[fallthrough]];
					default:
						insert_key(buf[i]);
				}
			}
			else // escape_sequence_buffer != "" means we are possibly in an escape sequence
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
						insert_key(ESC);
						escape_sequence_buffer.clear();
						if(buf[i] != ESC) // If we were called by the timer, the second key is inserted by the timer and should be ignored
							insert_key(buf[i]);
					}
				}
				else
				{
					// This is an escape sequence, add the new character and try to run it.
					escape_sequence_buffer+=buf[i];
					if(insert_esc_seq(escape_sequence_buffer))
					{
						// The escape sequence was recognised, so we are not in an escape sequence any more.
						escape_sequence_buffer.clear();
					}
				}
			}
		}
		draw();
		return accepted_lines.size();
	}
	//}}}

	//{{{
	std::vector<std::string>& get_accepted_lines(void)
	{
		return accepted_lines;
	}
	//}}}

	//{{{
	Repl(const std::string& prompt = "edit > ", milliseconds escape_sequence_timeout = milliseconds(100)) :
		escape_sequence_timeout(escape_sequence_timeout),
		prompt(prompt),
		line(""),
		curpos(0),
		accepted_lines(),
		history(),
		mode(Mode::INSERT)
	{
		changemode(1);
		draw();
	}

	~Repl(void)
	{
		changemode(0);
	}
	//}}}
};
