let SessionLoad = 1
let s:so_save = &so | let s:siso_save = &siso | set so=0 siso=0
let v:this_session=expand("<sfile>:p")
silent only
cd /tmp/asd-mox/home/mox/scratch/repl
if expand('%') == '' && !&modified && line('$') <= 1 && getline(1) == ''
  let s:wipebuf = bufnr('%')
endif
set shortmess=aoO
badd +0 CMakeLists.txt
badd +0 example.cpp
badd +0 repl.hpp
argglobal
silent! argdel *
$argadd CMakeLists.txt
edit example.cpp
set splitbelow splitright
wincmd _ | wincmd |
vsplit
1wincmd h
wincmd w
wincmd t
set winminheight=1 winminwidth=1 winheight=1 winwidth=1
exe 'vert 1resize ' . ((&columns * 159 + 160) / 320)
exe 'vert 2resize ' . ((&columns * 160 + 160) / 320)
argglobal
let s:l = 33 - ((32 * winheight(0) + 42) / 85)
if s:l < 1 | let s:l = 1 | endif
exe s:l
normal! zt
33
normal! 030|
wincmd w
argglobal
if bufexists('repl.hpp') | buffer repl.hpp | else | edit repl.hpp | endif
let s:l = 40 - ((39 * winheight(0) + 42) / 85)
if s:l < 1 | let s:l = 1 | endif
exe s:l
normal! zt
40
normal! 0
wincmd w
2wincmd w
exe 'vert 1resize ' . ((&columns * 159 + 160) / 320)
exe 'vert 2resize ' . ((&columns * 160 + 160) / 320)
tabnext 1
if exists('s:wipebuf') && getbufvar(s:wipebuf, '&buftype') isnot# 'terminal'
  silent exe 'bwipe ' . s:wipebuf
endif
unlet! s:wipebuf
set winheight=1 winwidth=20 winminheight=1 winminwidth=1 shortmess=filnxtToO
let s:sx = expand("<sfile>:p:r")."x.vim"
if file_readable(s:sx)
  exe "source " . fnameescape(s:sx)
endif
let &so = s:so_save | let &siso = s:siso_save
doautoall SessionLoadPost
unlet SessionLoad
" vim: set ft=vim :
