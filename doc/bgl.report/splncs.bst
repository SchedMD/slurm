% BibTeX bibliography style `splncs'

% An attempt to match the bibliography style required for use with
% numbered references in Springer Verlag's "Lecture Notes in Computer
% Science" series.  (See Springer's documentation for llncs.sty for
% more details of the suggested reference format.)  Note that this
% file will not work for author-year style citations.

% Use \documentclass{llncs} and \bibliographystyle{splncs}, and cite
% a reference with (e.g.) \cite{smith77} to get a "[1]" in the text.

% Copyright (C) 1999 Jason Noble.
% Last updated: Thursday 20 May 1999, 13:22:19
%
% Based on the BibTeX standard bibliography style `unsrt'

ENTRY
  { address
    author
    booktitle
    chapter
    edition
    editor
    howpublished
    institution
    journal
    key
    month
    note
    number
    organization
    pages
    publisher
    school
    series
    title
    type
    volume
    year
  }
  {}
  { label }

INTEGERS { output.state before.all mid.sentence after.sentence
           after.block after.authors between.elements}

FUNCTION {init.state.consts}
{ #0 'before.all :=
  #1 'mid.sentence :=
  #2 'after.sentence :=
  #3 'after.block :=
  #4 'after.authors :=
  #5 'between.elements :=
}

STRINGS { s t }

FUNCTION {output.nonnull}
{ 's :=
  output.state mid.sentence =
    { " " * write$ }
    { output.state after.block =
        { add.period$ write$
          newline$
          "\newblock " write$
        }
        {
          output.state after.authors =
          { ": " * write$
            newline$
            "\newblock " write$
          }
          { output.state between.elements =
              { ", " * write$ }
              { output.state before.all =
                   'write$
                  { add.period$ " " * write$ }
                  if$
              }
              if$
          }
          if$
        }
      if$
      mid.sentence 'output.state :=
    }
  if$
  s
}

FUNCTION {output}
{ duplicate$ empty$
    'pop$
    'output.nonnull
  if$
}

FUNCTION {output.check}
{ 't :=
  duplicate$ empty$
    { pop$ "empty " t * " in " * cite$ * warning$ }
    'output.nonnull
  if$
}

FUNCTION {output.bibitem}
{ newline$
  "\bibitem{" write$
  cite$ write$
  "}" write$
  newline$
  ""
  before.all 'output.state :=
}

FUNCTION {fin.entry}
{ write$
  newline$
}

FUNCTION {new.block}
{ output.state before.all =
    'skip$
    { after.block 'output.state := }
  if$
}

FUNCTION {stupid.colon}
{ after.authors 'output.state := }

FUNCTION {insert.comma}
{ output.state before.all =
    'skip$
    { between.elements 'output.state := }
  if$
}

FUNCTION {new.sentence}
{ output.state after.block =
    'skip$
    { output.state before.all =
        'skip$
        { after.sentence 'output.state := }
      if$
    }
  if$
}

FUNCTION {not}
{   { #0 }
    { #1 }
  if$
}

FUNCTION {and}
{   'skip$
    { pop$ #0 }
  if$
}

FUNCTION {or}
{   { pop$ #1 }
    'skip$
  if$
}

FUNCTION {new.block.checka}
{ empty$
    'skip$
    'new.block
  if$
}

FUNCTION {new.block.checkb}
{ empty$
  swap$ empty$
  and
    'skip$
    'new.block
  if$
}

FUNCTION {new.sentence.checka}
{ empty$
    'skip$
    'new.sentence
  if$
}

FUNCTION {new.sentence.checkb}
{ empty$
  swap$ empty$
  and
    'skip$
    'new.sentence
  if$
}

FUNCTION {field.or.null}
{ duplicate$ empty$
    { pop$ "" }
    'skip$
  if$
}

FUNCTION {emphasize}
{ duplicate$ empty$
    { pop$ "" }
    { "" swap$ * "" * }
  if$
}

FUNCTION {bold}
{ duplicate$ empty$
    { pop$ "" }
    { "\textbf{" swap$ * "}" * }
  if$
}

FUNCTION {parens}
{ duplicate$ empty$
    { pop$ "" }
    { "(" swap$ * ")" * }
  if$
}

INTEGERS { nameptr namesleft numnames }

FUNCTION {format.springer.names}
{ 's :=
  #1 'nameptr :=
  s num.names$ 'numnames :=
  numnames 'namesleft :=
    { namesleft #0 > }
    { s nameptr "{vv~}{ll}{, jj}{, f{.}.}" format.name$ 't :=
      nameptr #1 >
        { namesleft #1 >
            { ", " * t * }
            { numnames #1 >
                { ", " * }
                'skip$
              if$
              t "others" =
                { " et~al." * }
                { "" * t * }
              if$
            }
          if$
        }
        't
      if$
      nameptr #1 + 'nameptr :=
      namesleft #1 - 'namesleft :=
    }
  while$
}

FUNCTION {format.names}
{ 's :=
  #1 'nameptr :=
  s num.names$ 'numnames :=
  numnames 'namesleft :=
    { namesleft #0 > }
    { s nameptr "{vv~}{ll}{, jj}{, f.}" format.name$ 't :=
      nameptr #1 >
        { namesleft #1 >
            { ", " * t * }
            { numnames #2 >
                { "," * }
                'skip$
              if$
              t "others" =
                { " et~al." * }
                { " \& " * t * }
              if$
            }
          if$
        }
        't
      if$
      nameptr #1 + 'nameptr :=
      namesleft #1 - 'namesleft :=
    }
  while$
}

FUNCTION {format.authors}
{ author empty$
    { "" }
    { author format.springer.names }
  if$
}

FUNCTION {format.editors}
{ editor empty$
    { "" }
    { editor format.springer.names
      editor num.names$ #1 >
        { ", eds." * }
        { ", ed." * }
      if$
    }
  if$
}

FUNCTION {format.title}
{ title empty$
    { "" }
    { title "t" change.case$ }
  if$
}

FUNCTION {n.dashify}
{ 't :=
  ""
    { t empty$ not }
    { t #1 #1 substring$ "-" =
        { t #1 #2 substring$ "--" = not
            { "--" *
              t #2 global.max$ substring$ 't :=
            }
            {   { t #1 #1 substring$ "-" = }
                { "-" *
                  t #2 global.max$ substring$ 't :=
                }
              while$
            }
          if$
        }
        { t #1 #1 substring$ *
          t #2 global.max$ substring$ 't :=
        }
      if$
    }
  while$
}

FUNCTION {format.date}
{ year empty$
   { "there's no year in " cite$ * warning$ }
    'year
  if$
}

FUNCTION {format.btitle}
{ title emphasize
}

FUNCTION {tie.or.space.connect}
{ duplicate$ text.length$ #3 <
    { "~" }
    { " " }
  if$
  swap$ * *
}

FUNCTION {either.or.check}
{ empty$
    'pop$
    { "can't use both " swap$ * " fields in " * cite$ * warning$ }
  if$
}

FUNCTION {format.bvolume}
{ volume empty$
    { "" }
    { "Volume" volume tie.or.space.connect
      series empty$
        'skip$
        { " of " * series emphasize * }
      if$
      add.period$
      "volume and number" number either.or.check
    }
  if$
}

FUNCTION {format.number.series}
{ volume empty$
    { number empty$
        { series field.or.null }
        { output.state mid.sentence =
            { "number" }
            { "Number" }
          if$
          number tie.or.space.connect
          series empty$
            { "there's a number but no series in " cite$ * warning$ }
            { " in " * series * }
          if$
        }
      if$
    }
    { "" }
  if$
}

FUNCTION {format.edition}
{ edition empty$
    { "" }
    { output.state mid.sentence =
        { edition "l" change.case$ " edn." * }
        { edition "t" change.case$ " edn." * }
      if$
    }
  if$
}

INTEGERS { multiresult }

FUNCTION {multi.page.check}
{ 't :=
  #0 'multiresult :=
    { multiresult not
      t empty$ not
      and
    }
    { t #1 #1 substring$
      duplicate$ "-" =
      swap$ duplicate$ "," =
      swap$ "+" =
      or or
        { #1 'multiresult := }
        { t #2 global.max$ substring$ 't := }
      if$
    }
  while$
  multiresult
}

FUNCTION {format.pages}
{ pages empty$
    { "" }
    { pages multi.page.check
        { "" pages n.dashify tie.or.space.connect }
        { "" pages tie.or.space.connect }
      if$
    }
  if$
}

FUNCTION {format.vol}
{ volume bold
}

FUNCTION {pre.format.pages}
{ pages empty$
    'skip$
    { duplicate$ empty$
        { pop$ format.pages }
        { " " * pages n.dashify * }
      if$
    }
  if$
}

FUNCTION {format.chapter.pages}
{ chapter empty$
    'format.pages
    { type empty$
        { "chapter" }
        { type "l" change.case$ }
      if$
      chapter tie.or.space.connect
      pages empty$
        'skip$
        { " " * format.pages * }
      if$
    }
  if$
}

FUNCTION {format.in.ed.booktitle}
{ booktitle empty$
    { "" }
    { editor empty$
        { "In: " booktitle emphasize * }
        { "In " format.editors * ": " * booktitle emphasize * }
      if$
    }
  if$
}

FUNCTION {empty.misc.check}
{ author empty$ title empty$ howpublished empty$
  month empty$ year empty$ note empty$
  and and and and and
    { "all relevant fields are empty in " cite$ * warning$ }
    'skip$
  if$
}

FUNCTION {format.thesis.type}
{ type empty$
    'skip$
    { pop$
      type "t" change.case$
    }
  if$
}

FUNCTION {format.tr.number}
{ type empty$
    { "Technical Report" }
    'type
  if$
  number empty$
    { "t" change.case$ }
    { number tie.or.space.connect }
  if$
}

FUNCTION {format.article.crossref}
{ key empty$
    { journal empty$
        { "need key or journal for " cite$ * " to crossref " * crossref *
          warning$
          ""
        }
        { "In {\em " journal * "\/}" * }
      if$
    }
    { "In " key * }
  if$
  " \cite{" * crossref * "}" *
}

FUNCTION {format.crossref.editor}
{ editor #1 "{vv~}{ll}" format.name$
  editor num.names$ duplicate$
  #2 >
    { pop$ " et~al." * }
    { #2 <
        'skip$
        { editor #2 "{ff }{vv }{ll}{ jj}" format.name$ "others" =
            { " et~al." * }
            { " and " * editor #2 "{vv~}{ll}" format.name$ * }
          if$
        }
      if$
    }
  if$
}

FUNCTION {format.book.crossref}
{ volume empty$
    { "empty volume in " cite$ * "'s crossref of " * crossref * warning$
      "In "
    }
    { "Volume" volume tie.or.space.connect
      " of " *
    }
  if$
  " \cite{" * crossref * "}" *
}

FUNCTION {format.incoll.inproc.crossref}
{ editor empty$
  editor field.or.null author field.or.null =
  or
    { key empty$
        { booktitle empty$
            { "need editor, key, or booktitle for " cite$ * " to crossref " *
              crossref * warning$
              ""
            }
            { "" }
          if$
        }
        { "" }
      if$
    }
    { "" }
  if$
  " \cite{" * crossref * "}" *
}

FUNCTION {and.the.note}
{ note output
  note empty$
     'skip$
     { add.period$ }
  if$
}

FUNCTION {article}
{ output.bibitem
  format.authors "author" output.check
  stupid.colon
  format.title "title" output.check
  new.block
  crossref missing$
    { journal emphasize "journal" output.check
      format.vol output
      format.date parens output
      format.pages output
    }
    { format.article.crossref output.nonnull
      format.pages output
    }
  if$
  and.the.note
  fin.entry
}

FUNCTION {book}
{ output.bibitem
  author empty$
    { format.editors "author and editor" output.check }
    { format.authors output.nonnull
      crossref missing$
        { "author and editor" editor either.or.check }
        'skip$
      if$
    }
  if$
  stupid.colon
  format.btitle "title" output.check
  new.sentence
  crossref missing$
    { format.edition output
      format.bvolume output
      new.block
      format.number.series output
      new.sentence
      publisher "publisher" output.check
      address empty$
         'skip$
         { insert.comma }
      if$
      address output
      format.date parens output
    }
    { format.book.crossref output.nonnull
    }
  if$
  and.the.note
  fin.entry
}

FUNCTION {booklet}
{ output.bibitem
  format.authors output
  stupid.colon
  format.title "title" output.check
  howpublished address new.block.checkb
  howpublished output
  address empty$
     'skip$
     { insert.comma }
  if$
  address output
  format.date parens output
  and.the.note
  fin.entry
}

FUNCTION {inbook}
{ output.bibitem
  author empty$
    { format.editors "author and editor" output.check }
    { format.authors output.nonnull
      crossref missing$
        { "author and editor" editor either.or.check }
        'skip$
      if$
    }
  if$
  stupid.colon
  crossref missing$
    { chapter output
      new.block
      format.number.series output
      new.sentence
      "In:" output
      format.btitle "title" output.check
      new.sentence
      format.edition output
      format.bvolume output
      publisher "publisher" output.check
      address empty$
         'skip$
         { insert.comma }
      if$
      address output
      format.date parens output
    }
    { chapter output
      new.block
      format.incoll.inproc.crossref output.nonnull
    }
  if$
  format.pages output
  and.the.note
  fin.entry
}

FUNCTION {incollection}
{ output.bibitem
  format.authors "author" output.check
  stupid.colon
  format.title "title" output.check
  new.block
  crossref missing$
    { format.in.ed.booktitle "booktitle" output.check
      new.sentence
      format.bvolume output
      format.number.series output
      new.block
      format.edition output
      publisher "publisher" output.check
      address empty$
         'skip$
         { insert.comma }
      if$
      address output
      format.date parens output
      format.pages output
    }
    { format.incoll.inproc.crossref output.nonnull
      format.chapter.pages output
    }
  if$
  and.the.note
  fin.entry
}

FUNCTION {inproceedings}
{ output.bibitem
  format.authors "author" output.check
  stupid.colon
  format.title "title" output.check
  new.block
  crossref missing$
    { format.in.ed.booktitle "booktitle" output.check
      new.sentence
      format.bvolume output
      format.number.series output
      address empty$
        { organization publisher new.sentence.checkb
          organization empty$
             'skip$
             { insert.comma }
          if$
          organization output
          publisher empty$
             'skip$
             { insert.comma }
          if$
          publisher output
          format.date parens output
        }
        { insert.comma
          address output.nonnull
          organization empty$
             'skip$
             { insert.comma }
          if$
          organization output
          publisher empty$
             'skip$
             { insert.comma }
          if$
          publisher output
          format.date parens output
        }
      if$
    }
    { format.incoll.inproc.crossref output.nonnull
    }
  if$
  format.pages output
  and.the.note
  fin.entry
}

FUNCTION {conference} { inproceedings }

FUNCTION {manual}
{ output.bibitem
  author empty$
    { organization empty$
        'skip$
        { organization output.nonnull
          address output
        }
      if$
    }
    { format.authors output.nonnull }
  if$
  stupid.colon
  format.btitle "title" output.check
  author empty$
    { organization empty$
        { address new.block.checka
          address output
        }
        'skip$
      if$
    }
    { organization address new.block.checkb
      organization output
      address empty$
         'skip$
         { insert.comma }
      if$
      address output
    }
  if$
  new.sentence
  format.edition output
  format.date parens output
  and.the.note
  fin.entry
}

FUNCTION {mastersthesis}
{ output.bibitem
  format.authors "author" output.check
  stupid.colon
  format.title "title" output.check
  new.block
  "Master's thesis" format.thesis.type output.nonnull
  school empty$
     'skip$
     { insert.comma }
  if$
  school "school" output.check
  address empty$
     'skip$
     { insert.comma }
  if$
  address output
  format.date parens output
  and.the.note
  fin.entry
}

FUNCTION {misc}
{ output.bibitem
  format.authors "author" output.check
  stupid.colon
  format.title "title" output.check
  howpublished new.block.checka
  howpublished output
  format.date parens output
  and.the.note
  fin.entry
  empty.misc.check
}

FUNCTION {phdthesis}
{ output.bibitem
  format.authors "author" output.check
  stupid.colon
  format.btitle "title" output.check
  new.block
  "PhD thesis" format.thesis.type output.nonnull
  school empty$
     'skip$
     { insert.comma }
  if$
  school "school" output.check
  address empty$
     'skip$
     { insert.comma }
  if$
  address output
  format.date parens output
  and.the.note
  fin.entry
}

FUNCTION {proceedings}
{ output.bibitem
  editor empty$
    { organization empty$
        { "" }
        { organization output
          stupid.colon }
      if$
    }
    { format.editors output.nonnull
      stupid.colon
    }
  if$
  format.btitle "title" output.check
  new.block
  crossref missing$
    { format.in.ed.booktitle "booktitle" output.check
      new.sentence
      format.bvolume output
      format.number.series output
      address empty$
        { organization publisher new.sentence.checkb
          organization empty$
             'skip$
             { insert.comma }
          if$
          organization output
          publisher empty$
             'skip$
             { insert.comma }
          if$
          publisher output
          format.date parens output
        }
        { insert.comma
          address output.nonnull
          organization empty$
             'skip$
             { insert.comma }
          if$
          organization output
          publisher empty$
             'skip$
             { insert.comma }
          if$
          publisher output
          format.date parens output
        }
      if$
    }
    { format.incoll.inproc.crossref output.nonnull
    }
  if$
  and.the.note
  fin.entry
}

FUNCTION {techreport}
{ output.bibitem
  format.authors "author" output.check
  stupid.colon
  format.title "title" output.check
  new.block
  format.tr.number output.nonnull
  institution empty$
     'skip$
     { insert.comma }
  if$
  institution "institution" output.check
  address empty$
     'skip$
     { insert.comma }
  if$
  address output
  format.date parens output
  and.the.note
  fin.entry
}

FUNCTION {unpublished}
{ output.bibitem
  format.authors "author" output.check
  stupid.colon
  format.title "title" output.check
  new.block
  note "note" output.check
  format.date parens output
  fin.entry
}

FUNCTION {default.type} { misc }

MACRO {jan} {"January"}

MACRO {feb} {"February"}

MACRO {mar} {"March"}

MACRO {apr} {"April"}

MACRO {may} {"May"}

MACRO {jun} {"June"}

MACRO {jul} {"July"}

MACRO {aug} {"August"}

MACRO {sep} {"September"}

MACRO {oct} {"October"}

MACRO {nov} {"November"}

MACRO {dec} {"December"}

MACRO {acmcs} {"ACM Computing Surveys"}

MACRO {acta} {"Acta Informatica"}

MACRO {cacm} {"Communications of the ACM"}

MACRO {ibmjrd} {"IBM Journal of Research and Development"}

MACRO {ibmsj} {"IBM Systems Journal"}

MACRO {ieeese} {"IEEE Transactions on Software Engineering"}

MACRO {ieeetc} {"IEEE Transactions on Computers"}

MACRO {ieeetcad}
 {"IEEE Transactions on Computer-Aided Design of Integrated Circuits"}

MACRO {ipl} {"Information Processing Letters"}

MACRO {jacm} {"Journal of the ACM"}

MACRO {jcss} {"Journal of Computer and System Sciences"}

MACRO {scp} {"Science of Computer Programming"}

MACRO {sicomp} {"SIAM Journal on Computing"}

MACRO {tocs} {"ACM Transactions on Computer Systems"}

MACRO {tods} {"ACM Transactions on Database Systems"}

MACRO {tog} {"ACM Transactions on Graphics"}

MACRO {toms} {"ACM Transactions on Mathematical Software"}

MACRO {toois} {"ACM Transactions on Office Information Systems"}

MACRO {toplas} {"ACM Transactions on Programming Languages and Systems"}

MACRO {tcs} {"Theoretical Computer Science"}

READ

STRINGS { longest.label }

INTEGERS { number.label longest.label.width }

FUNCTION {initialize.longest.label}
{ "" 'longest.label :=
  #1 'number.label :=
  #0 'longest.label.width :=
}

FUNCTION {longest.label.pass}
{ number.label int.to.str$ 'label :=
  number.label #1 + 'number.label :=
  label width$ longest.label.width >
    { label 'longest.label :=
      label width$ 'longest.label.width :=
    }
    'skip$
  if$
}

EXECUTE {initialize.longest.label}

ITERATE {longest.label.pass}

FUNCTION {begin.bib}
{ preamble$ empty$
    'skip$
    { preamble$ write$ newline$ }
  if$
  "\begin{thebibliography}{"  longest.label  * "}" * write$ newline$
}

EXECUTE {begin.bib}

EXECUTE {init.state.consts}

ITERATE {call.type$}

FUNCTION {end.bib}
{ newline$
  "\end{thebibliography}" write$ newline$
}

EXECUTE {end.bib}



