"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
"
" Vim syntax file for completion for Slurm
"
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
"  Copyright (C) 2012 Damien Franois. <damien.francois@uclouvain.Be>
"  Written by Damien Franois. <damien.francois@uclouvain.Be>.
"
"  This file is part of Slurm, a resource management program.
"  For details, see <https://slurm.schedmd.com/>.
"  Please also read the included file: DISCLAIMER.
"
"  Slurm is free software; you can redistribute it and/or modify it under
"  the terms of the GNU General Public License as published by the Free
"  Software Foundation; either version 2 of the License, or (at your option)
"  any later version.
"
"  In addition, as a special exception, the copyright holders give permission
"  to link the code of portions of this program with the OpenSSL library under
"  certain conditions as described in each individual source file, and
"  distribute linked combinations including the two. You must obey the GNU
"  General Public License in all respects for all of the code used other than
"  OpenSSL. If you modify file(s) with this exception, you may extend this
"  exception to your version of the file(s), but you are not obligated to do
"  so. If you do not wish to do so, delete this exception statement from your
"  version.  If you delete this exception statement from all source files in
"  the program, then also delete it here.
"
"  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
"  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
"  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
"  details.
"
"  You should have received a copy of the GNU General Public License along
"  with Slurm; if not, write to the Free Software Foundation, Inc.,
"  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
"
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""

" handling /bin/sh with is_kornshell/is_sh {{{1
" b:is_sh is set when "#! /bin/sh" is found;
" However, it often is just a masquerade by bash (typically Linux)
" or kornshell (typically workstations with Posix "sh").
" So, when the user sets "is_bash" or "is_kornshell",
" a b:is_sh is converted into b:is_bash/b:is_kornshell,
" respectively.
if !exists("b:is_kornshell") && !exists("b:is_bash")
  if exists("g:is_posix") && !exists("g:is_kornshell")
   let g:is_kornshell= g:is_posix
  endif
  if exists("g:is_kornshell")
    let b:is_kornshell= 1
    if exists("b:is_sh")
      unlet b:is_sh
    endif
  elseif exists("g:is_bash")
    let b:is_bash= 1
    if exists("b:is_sh")
      unlet b:is_sh
    endif
  else
    let b:is_sh= 1
  endif
endif

" Slurm: {{{1
" ===================
" Slurm SBATCH comments are one liners beginning with #SBATCH and containing
" the keyword (i.e.SBATCH), one option (here only options starting with -- are
" considered), and one optional value.
syn region	shSlurmComment start="^#SBATCH" end="\n" oneline contains=shSlurmKeyword,shSlurmOption,shSlurmValue
" all shSlurmString are suspect; they probably could be narrowed down to more
" specific regular expressions. Typical example is --mail-type or --begin
syn match 	shSlurmKeyword 	contained	'#SBATCH\s*'
syn match 	shSlurmOption	contained	'--account=' nextgroup=shSlurmString
syn match 	shSlurmOption	contained	'--acctg-freq=' nextgroup=shSlurmNumber
syn match 	shSlurmOption	contained	'--extra-node-info=' nextgroup=shSlurmNodeInfo
syn match 	shSlurmOption	contained	'--socket-per-node=' nextgroup=shSlurmNumber
syn match 	shSlurmOption	contained	'--cores-per-socket=' nextgroup=shSlurmNumber
syn match 	shSlurmOption	contained	'--threads-per-core=' nextgroup=shSlurmNumber
syn match 	shSlurmOption	contained	'--begin=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--comment=' nextgroup=shSlurmIdentifier
syn match    	shSlurmOption	contained	'--constraint=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--contiguous'
syn match    	shSlurmOption	contained	'--cpu-bind==' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--cpus-per-task=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--dependency=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--workdir=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--error=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--exclusive'
syn match    	shSlurmOption	contained	'--nodefile=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--get-user-env'
syn match    	shSlurmOption	contained	'--get-user-env=' nextgroup=shSlurmEnv
syn match    	shSlurmOption	contained	'--gid=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--hint=' nextgroup=shSlurmHint
syn match    	shSlurmOption	contained	'--immediate' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--input=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--job-name=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--job-id=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--no-kill'
syn match    	shSlurmOption	contained	'--licences=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--distribution=' nextgroup=shSlurmDist
syn match 	shSlurmOption	contained	'--mail-user=' nextgroup=shSlurmEmail
syn match 	shSlurmOption	contained	'--mail-type=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--mem=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--mem-per-cpu=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--mem-bind=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--mincores=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--mincpus=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--minsockets=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--minthreads=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--nodes=' nextgroup=shSlurmInterval
syn match    	shSlurmOption	contained	'--ntasks=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--network=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--nice'
syn match    	shSlurmOption	contained	'--nice=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--no-requeue'
syn match 	shSlurmOption	contained	'--ntasks-per-core=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--ntasks-per-socket=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--ntasks-per-node=' nextgroup=shSlurmNumber
syn match    	shSlurmOption	contained	'--overcommit'
syn match    	shSlurmOption	contained	'--output=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--open-mode=' nextgroup=shSlurmMode
syn match    	shSlurmOption	contained	'--partition=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--propagate'
syn match    	shSlurmOption	contained	'--propagate=' nextgroup=shSlurmPropag
syn match    	shSlurmOption	contained	'--quiet'
syn match    	shSlurmOption	contained	'--requeue'
syn match    	shSlurmOption	contained	'--reservation=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--share'
syn match    	shSlurmOption	contained	'--signal=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--time=' nextgroup=shSlurmDuration
syn match    	shSlurmOption	contained	'--tmp=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--uid=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--nodelist=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--wckey=' nextgroup=shSlurmString
syn match    	shSlurmOption	contained	'--wrap=' nextgroup=shSlurmString
syn match 	shSlurmOption	contained	'--exclude=' nextgroup=shSlurmString
syn region	shSlurmValue start="=" end="$" contains=shSlurmNoshSlurmEnvdeInfo,shSlurmString,shSlurmMailType,shSlurmIdentifier,shSlurmEnv,shSlurmHint,shSlurmMode,shSlurmPropag,shSlurmInterval,shSlurmDist,shSlurmEmail
syn match 	shSlurmNumber	contained	'\d\d*'
syn match 	shSlurmDuration	contained	'\d\d*\(:\d\d\)\{,2}'
syn match 	shSlurmNodeInfo	contained	'\d\d*\(:\d\d*\)\{,2}'
syn match 	shSlurmDuration	contained	'\d\d*-\d\=\d\(:\d\d\)\{,2}'
syn match 	shSlurmInterval	contained	'\d\d*\(-\d*\)\='
syn match 	shSlurmString	contained	'.*'
syn match 	shSlurmEnv	contained	'\d*L\=S\='
syn keyword 	shSlurmHint	contained 	 compute_bound memory_bound nomultithread multithread
syn keyword 	shSlurmMode	contained 	 append truncate
syn keyword 	shSlurmPropag	contained 	 ALL AS CORE CPU DATA FSIZE MEMLOCK NOFILE CPROC RSS STACK
syn keyword 	shSlurmDist	contained 	 block cyclic arbitrary
syn match	shSlurmDist	contained	'plane\(=.*\)\='
syn match	shSlurmEmail	contained	'[-a-zA-Z0-9.+]*@[-a-zA-Z0-9.+]*'

"Anything that is not recognized is marked as error
hi def link shSlurmComment	Error
"The #SBATCH keyword
hi def link shSlurmKeyword	Function
"The option
hi def link shSlurmOption	Operator
"The values
hi def link shSlurmDuration	Special
hi def link shSlurmString	Special
hi def link shSlurmMailType	Special
hi def link shSlurmNumber	Special
hi def link shSlurmSep	Special
hi def link shSlurmNodeInfo	Special
hi def link shSlurmEnv	Special
hi def link shSlurmHint	Special
hi def link shSlurmMode	Special
hi def link shSlurmPropag	Special
hi def link shSlurmInterval	Special
hi def link shSlurmDist	Special
hi def link shSlurmEmail	Special
