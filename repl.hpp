#include <iostream>
#include <termios.h> 
#include <unistd.h>
#include <map>
#include <functional>
#include <chrono>
//#include <stropts.h>
//#include <fcntl.h>
//#include <cctype>
//#include <memory>
///#include <optional>

#define ESC '\33'
#define DEL '\177'

class Repl
{
	//{{{ Internal state
	using milliseconds = std::chrono::duration<int64_t, std::milli>;
	using steady_clock = std::chrono::steady_clock;
	milliseconds escape_sequence_timeout;

	std::string prompt;
	std::string line;
	//{{{
	class Curpos
	{
		const Repl& repl;
		int position;
		//{{{
		void check(void)
		{
			if((repl.mode==INSERT || repl.mode==INSERT_ESCAPED) && (static_cast<int>(repl.line.length()) <= position)) position = repl.line.length();
			if((repl.mode==NORMAL || repl.mode==NORMAL_ESCAPED) && (static_cast<int>(repl.line.length()) <= position)) position = repl.line.length()-1;
			if(position <= 0) position=0;
		}
		//}}}
		public:
		//{{{

		Curpos(const Repl& repl, int position=0) : repl(repl), position(position)
		{
			check();
		}

		Curpos(const Curpos& other) : repl(other.repl), position(other.position) {}




		Curpos& operator=(int new_position) 
		{
			position=new_position;
			check();
			return *this;
		}
		const Curpos operator+(int rhs)
		{
			Curpos tmp(*this);
			tmp += rhs;
			return tmp;
		}
		const Curpos operator-(int rhs)
		{
			Curpos tmp(*this);
			tmp -= rhs;
			return tmp;
		}
		Curpos& operator+=(int rhs)
		{
			position+=rhs;
			check();
			return *this;
		}
		Curpos& operator-=(int rhs)
		{
			position-=rhs;
			check();
			return *this;
		}
		Curpos& operator++() //Praeinkrement
		{
			position++;
			check();
			return *this;
		}
		const Curpos operator++(int) //Postinkrement	
		{
			Curpos tmp(*this);
			++(*this);
			return tmp;
		}
		Curpos& operator--() //Praeinkrement
		{
			position--;
			check();
			return *this;
		}
		const Curpos operator--(int) //Postinkrement	
		{
			Curpos tmp(*this);
			--(*this);
			return tmp;
		}
		bool operator==(int rhs) const
		{
			return position==rhs;
		}
		bool operator!=(int rhs) const
		{
			return position!=rhs;
		}
		bool operator<=(int rhs) const
		{
			return position<=rhs;
		}
		bool operator>=(int rhs) const
		{
			return position>=rhs;
		}
		bool operator<(int rhs) const
		{
			return position<rhs;
		}
		bool operator>(int rhs) const
		{
			return position>rhs;
		}

		operator bool(void) const
		{
			return position;
		}
		operator int(void) const
		{
			return position;
		}
		operator long unsigned int(void) const
		{
			return static_cast<long unsigned int>(position);
		}
		//}}}
	} curpos;
	//}}}

	std::vector<std::string> accepted_lines;
	std::vector<std::string> history;
	std::optional<std::function<void(std::string,int)>> word_completer;
	std::optional<std::function<void(std::string,int)>> multi_completer;
	std::optional<std::function<void(std::vector<std::string>,std::string,int)>> hist_completer;
	std::optional<std::function<void(std::vector<std::string>,std::string,int)>> global_hist_fwd;
	std::optional<std::function<void(std::vector<std::string>,std::string,int)>> global_hist_bwd;
	std::optional<std::function<void(std::vector<std::string>,std::string,int)>> matching_hist_fwd;
	std::optional<std::function<void(std::vector<std::string>,std::string,int)>> matching_hist_bwd;

	//{{{
	enum Mode
	{
		INSERT         = 0, // Default: typed keys are appended to the command line
		NORMAL         = 1, // After pressing ESC: keys are interpreted as commands
		INSERT_ESCAPED = 2, // Insert mode while processing an escape sequence
		NORMAL_ESCAPED = 3	// Normal mode while processing an escape sequence
	} mode;
	//}}}
	//}}}

	void change_terminal_mode(int dir);

	//{{{ On keypress functions

	using on_keypress_function = std::function<bool(char)>;
	using Key = char;
	using action_map = std::map<std::string, on_keypress_function>;

	// All the actions a key press can trigger. Yes, this is one huge std::map of lambdas. C++ does not offer reflection and this way there is only one definition.
	std::map<std::string, on_keypress_function> actions = { // TODO: Replace with normal functions when reflection becomes available in C++
		//{{{ Cursor movement

		{ "move_cursor_begin",        [this](char  ){ curpos=0; return false; }},
		{ "move_cursor_left",         [this](char  ){ curpos--; return false; }},
		{ "move_cursor_right",        [this](char  ){ curpos++; return false; }},
		{ "move_cursor_end",          [this](char  ){ curpos=line.length(); return false; }},
		{ "move_cursor_word_begin",   [this](char  ){ for(; curpos&&line[curpos]!=' '; --curpos); return false; }}, // TODO
		{ "move_cursor_word_end",     [this](char  ){ for(; curpos<static_cast<int>(line.length())&&line[curpos]!=' '; ++curpos); return false; }}, // TODO
		{ "move_cursor_word_next",    [this](char  ){ for(; curpos<static_cast<int>(line.length())&&line[curpos]!=' '; ++curpos); return false; }}, // TODO
		//}}}
		//{{{ Mode change

		{ "changemode_insert_begin",  [this](char  ){ mode=INSERT; curpos=0; return false; }}, // TODO: Check
		{ "changemode_insert",        [this](char  ){ mode=INSERT; return false; }},
		{ "changemode_append",        [this](char  ){ mode=INSERT; curpos++; return false; }}, // TODO: Check
		{ "changemode_append_end",    [this](char  ){ mode=INSERT; curpos=line.length(); return false; }}, // TODO: Check
		{ "changemode_normal",        [this](char  ){ mode=NORMAL; curpos--; return false; }},
		//}}}
		//{{{ Insert/delete character
 
		{ "add_char",                 [this](char c){ line.insert(curpos,1,c); curpos++; return false; }},
		{ "delete_char",              [this](char  ){ if(line.length()) line.erase(curpos,1); return false; }},
		{ "backspace",                [this](char  ){ if(line.length()) line.erase(--curpos,1); return false; }},
		//}}}
		//{{{ Accept line

		{ "accept",                   [this](char  ){ curpos=0; history.push_back(line); accepted_lines.push_back(line); line.clear(); return true; }},
		{ "accept_no_add_history",    [this](char  ){ curpos=0; accepted_lines.push_back(line); line.clear(); return true; }},
		//}}}
		//{{{ Complete

		{ "complete_single_word",     [this](char  ){ if(word_completer)   std::invoke(*word_completer,      line, curpos); return false; }},
		{ "complete_multi_word",      [this](char  ){ if(multi_completer)  std::invoke(*multi_completer,     line, curpos); return false; }},
		{ "complete_history",         [this](char  ){ if(hist_completer)   std::invoke(*hist_completer,history,line,curpos); return false;}},
		//}}}
		//{{{ History search

		{ "search_global_hist_fwd",   [this](char  ){ if(global_hist_fwd)  std::invoke(*global_hist_fwd,  history,line,curpos); return false; }},
		{ "search_global_hist_bwd",   [this](char  ){ if(global_hist_fwd)  std::invoke(*global_hist_fwd,  history,line,curpos); return false; }},
		{ "search_matching_hist_fwd", [this](char  ){ if(matching_hist_fwd)std::invoke(*matching_hist_fwd,history,line,curpos); return false; }},
		{ "search_matching_hist_bwd", [this](char  ){ if(matching_hist_bwd)std::invoke(*matching_hist_bwd,history,line,curpos); return false; }},
		//}}}
	};

	std::array<action_map, 4> mappings; // Mappings for all 4 modes (INSERT, NORMAL, INSERT_ESCAPED, NORMAL_ESCAPED)
	//}}}

	void insert_key(Key key);
	bool insert_key(std::string key);

	void draw(void);

	public:

	void map(Mode mode, Key key_combo, std::string action);
	void map(Mode mode, std::string key_combo, std::string action);

	void default_mappings(void);

	int insert(char buf[], std::size_t len);

	std::vector<std::string>& get_accepted_lines(void);

	Repl(const std::string& prompt = "edit > ", milliseconds escape_sequence_timeout = milliseconds(100));
	~Repl(void);
};
