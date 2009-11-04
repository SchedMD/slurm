%%=============================================================================
%% prosper.cls
%% Copyright 2000-2001 Frédéric Goualard and Peter Møller Neergaard
%%
%% This program may be distributed and/or modified under the
%% conditions of the LaTeX Project Public License, either version 1.2
%% of this license or (at your option) any later version.
%% The latest version of this license is in
%%   http://www.latex-project.org/lppl.txt
%% and version 1.2 or later is part of all distributions of LaTeX 
%% version 1999/12/01 or later.
%%
%% This program consists of the files listed in manifest.txt
%%
%% CVSId : $Id$
%%=============================================================================

\def\Prosper@Version{2001/07/17, v. 1.5}
\NeedsTeXFormat{LaTeX2e}[1995/12/01]
\ProvidesClass{prosper}[\Prosper@Version]
\typeout{(c) 2000-2001 Frederic Goualard, EPFL, Switzerland}
\typeout{          and Peter Møller Neergaard, Boston Univ., USA}
\typeout{CVSId: $Id$}

\newif\ifDVItoPS
\DeclareOption{ps}{\global\DVItoPStrue}
\DeclareOption{pdf}{\global\DVItoPSfalse}

\newif\ifisDraft
\DeclareOption{draft}{\global\isDrafttrue%
  \PassOptionsToClass{draft}{seminar}}
\DeclareOption{final}{\global\isDraftfalse%
  \PassOptionsToClass{final}{seminar}}

\newif\ifinColor
\DeclareOption{slideColor}{\global\inColortrue}
\DeclareOption{slideBW}{\global\inColorfalse}

\newif\ifallPages
\DeclareOption{total}{\global\allPagestrue}
\DeclareOption{nototal}{\global\allPagesfalse}

\newif\ifcolorBG
\DeclareOption{nocolorBG}{\global\colorBGfalse}
\DeclareOption{colorBG}{\global\colorBGtrue}

\newif\ifAccumulate
\DeclareOption{accumulate}{\global\Accumulatetrue}
\DeclareOption{noaccumulate}{\global\Accumulatefalse}

\newif\ifnoFooter
\DeclareOption{noFooter}{\global\noFootertrue}
\noFooterfalse

\newif\if@distiller
\@distillerfalse
\DeclareOption{distiller}{\global\@distillertrue}

\DeclareOption{YandY}{\gdef\@pdf@driver{dvipsone}}
\DeclareOption{ps2pdf}{\gdef\@pdf@driver{ps2pdf}}
\DeclareOption{vtex}{\gdef\@pdf@driver{vtex}}

%% Default style file = default
\def\Style@chosen{default}
\DeclareOption*{\global\let\Style@chosen=\CurrentOption}

\ExecuteOptions{final,slideBW,total,nocolorBG,noaccumulate,ps,ps2pdf}
\ProcessOptions\relax

\RequirePackage{graphicx} % MUST be loaded *before* seminar

\LoadClass[portrait,semlayer,semcolor,leqno]{seminar}
\InputIfFileExists{seminar.bug}{}\relax
\InputIfFileExists{seminar.bg2}{}\relax
\InputIfFileExists{seminar-bg2-lepennec.fix}{}\relax

\RequirePackage[\@pdf@driver,bookmarks,%
                letterpaper,%
%                pdfmenubar=false,%
                pdfhighlight=/I,%
%                pdftoolbar=false,%
                pdffitwindow=true,%
                pdfcenterwindow=true,%
%                pdfwindowui=false,%
                menucolor=menucolor,%
%                pdfview=Fit,%
                pdfstartview=Fit]{hyperref}

\rotateheaderstrue
%\slidewidth=222mm
%\slideheight=150mm
%\slidewidth=11in
%\slideheight=8.5in
\renewcommand{\slideleftmargin}{0.01in}

% overlays environment
% Parameter: # of slides in the overlay
% slide 0 is the one for PostScript file
\newbox\theoverlays
\newcounter{overlaysCount}
\newcounter{limitOverlays}

\newcount\@tempoLimit \@tempoLimit=0
\newif\ifcollapsedBookmarks
% Default: collapsed bookmarks tree
\collapsedBookmarkstrue
% Add a bookmark for each master slide
\def\@addBookmarkOnSlide#1#2{%
  \ifnum#1=0
  \def\Hy@temp{#2}% 
    \pdfmark{pdfmark=/OUT,
      Raw={/Page \thetrueSlideCounter 
        /View [ /XYZ null null null ]
        /Title (\expandafter\strip@prefix\meaning\Hy@temp)}
    }%
  \else
  \@tempoLimit=#1%
  \advance \@tempoLimit by -1
  \ifcollapsedBookmarks
    \@tempoLimit=-\@tempoLimit
  \fi
  \def\Hy@temp{#2}% 
    \pdfmark{pdfmark=/OUT,
      Raw={/Count \number\@tempoLimit /Page \thetrueSlideCounter 
        /View [ /XYZ null null null ]
        /Title (\expandafter\strip@prefix\meaning\Hy@temp)}
    }%
  \fi
}


%% Some code to be added when creating a PDF file
\def\Cropping@Box{0 0 595 890} % By default: cropping to a4 paper dimensions
\ifDVItoPS
\else
  \AtEndOfClass{%
    \hypersetup{pdfpagescrop=\Cropping@Box}%
  }
\if@distiller
\else
  \AtBeginDvi{%
      \pdfmark{pdfmark=/PAGES,
        Raw={/Rotate 90}}
  }%
\fi
\fi
\newcommand{\PDFCroppingBox}[1]{%
  \gdef\Cropping@Box{#1}
  }

%% PDFtransition
%% Transition from one page to another.
%% Possible values:
%%       (Split, Blinds, Box, Wipe, Dissolve, Glitter, R)
\def\Split{} \def\Blinds{} \def\Box{} \def\Wipe{} \def\Dissolve{}
\def\Glitter{} \def\R{} \def\Replace{}
\newcommand{\PDFtransition}[1]{%
  \@ifundefined{#1}
  {%
    \ClassError{prosper}{Undefined transition #1}{%
      Possible values: Split, Blinds, Box, Wipe, Dissolve, Glitter, R}}
  {%
   \hypersetup{pdfpagetransition={#1}}
  }%
}

%% Default transition between pages (for values, see \PDFtransition)
\def\@defaultTransition{R}
\newcommand{\DefaultTransition}[1]{\gdef\@defaultTransition{#1}}


\let\@cartouche\@empty
%% \slideCaption : Definition of caption to appear on every slide.
\newcommand{\slideCaption}[1]{\gdef\@cartouche{#1}}

%%-----------------------------------------------------------------------------
%% Definitions for fonts and colors
%% #1: font and color if slide in color
%% #2: font and color if slide in BW
\def\@fontTitleColor{\ClassError{prosper}{%
    no font specified for title in color}{Use macro \string\FontTitle}}
\def\@fontTitleBW{\ClassError{prosper}{%
    no font specified for title in B\string&W}{Use macro \string\FontTitle}}
\def\@fontSubtitleColor{\@fontTitleColor\fontsize{16pt}{16pt}\selectfont\itshape}
\def\@fontSubtitleBW{\@fontTitleBW\fontsize{14pt}{16pt}\selectfont\itshape}
\def\@fontTextColor{\ClassError{prosper}{%
    no font specified for texte in color}{Use macro \string\FontText}}
\def\@fontTextBW{\ClassError{prosper}{%
    no font specified for text in B\string&W}{Use macro \string\FontText}}
\def\@colorFoot{\black}
\newcommand{\FontTitle}[2]{%
  \gdef\@fontTitleColor{#1}% 
  \gdef\@fontTitleBW{#2}}
\newcommand{\FontSubtitle}[2]{%
  \gdef\@fontSubtitleColor{#1}%
  \gdef\@fontSubtitleBW{#2}}
\newcommand{\FontText}[2]{%
  \gdef\@fontTextColor{#1}% 
  \gdef\@fontTextBW{#2}}
\newcommand{\ColorFoot}[1]{\gdef\@colorFoot{#1}}

\newcommand{\fontTitle}[1]{%
  {\ifinColor\@fontTitleColor\else\@fontTitleBW\fi #1}}
\newcommand{\fontText}[1]{%
  {\ifinColor\@fontTextColor\else\@fontTextBW\fi #1}}
%%- End of definitions for fonts and colors -----------------------------------

% Width of the text area
\newlength{\slideWidth}

\def\@titleSpacing{\vspace*{10pt}}
\newcommand{\titleSpacing}[1]{\gdef\@titleSpacing{#1}}

\def\@myendminipage{%
  \global\let\@oldendminipage=\endminipage%
  \global\let\endminipage=\@newendminipage%
}

\def\@normalendminipage{%
   \global\let\endminipage=\@oldendminipage%
}
\def\@newendminipage{%
  \par
    \unskip
    \ifvoid\@mpfootins\else
      \vskip\skip\@mpfootins
      \normalcolor
      \footnoterule
      \unvbox\@mpfootins
    \fi
    \@minipagefalse   %% added 24 May 89
  \color@endgroup
  \egroup
  \typeout{Vertical size: \the\ht\@tempboxa}
  \expandafter\@iiiparbox\@mpargs{\unvbox\@tempboxa}}

%% Redefinition of the slide environment
\newcounter{trueSlideCounter}
\def\thetrueslideCounter{\arabic{trueSlideCounter}}
\let\slide@seminar=\slide
\let\endslide@seminar=\endslide
\let\slide=\relax
\let\endslide=\relax
\newenvironment{SlideNormal}[2][\@defaultTransition]{%
  \stepcounter{trueSlideCounter}%
  \PDFtransition{#1}%
  \begin{slide@seminar}%
    \def\@tempa{#2}%
    \ifx\@tempa\@empty
    \else
       \@addBookmarkOnSlide{0}{#2}%
       \slidetitle{#2}%
    \fi
    \begin{posit@Box}%
      \begin{minipage}{\slideWidth}%    
        \begin{raggedright}%
        \@DefMyItem
        \ifinColor\@fontTextColor\else\@fontTextBW\fi
        }{%
      \par\end{raggedright}% 
    \end{minipage}
  \end{posit@Box}\end{slide@seminar}}
% Slides in overlays
\newenvironment{SlideOverlay}[2][\@defaultTransition]{%
  \ifDVItoPS
      \begin{slide@seminar}%
        \slidetitle{#2}%
        \begin{posit@Box}%
          \begin{minipage}{\slideWidth}%
            \begin{raggedright}%
            \@DefMyItem
            \ifinColor\@fontTextColor\else\@fontTextBW\fi
  \else %% DVI -> PDF
      \stepcounter{trueSlideCounter}%
      \PDFtransition{#1}%
      \begin{slide@seminar}%
        \@prepareResetCounters
        \def\@tempa{#2}%
        \ifx\@tempa\@empty
        \else
          \@ifundefined{@okForBookmark}{%
            \gdef\@okForBookmark{}%
            \@addBookmarkOnSlide{\number\c@limitOverlays}{#2}%
          }{\@addBookmarkOnSlide{0}{#2}}%
          \slidetitle{#2}%
        \fi
        \begin{posit@Box}%
          \begin{minipage}{\slideWidth}%
            \begin{raggedright}%
            \@DefMyItem
            \ifinColor\@fontTextColor\else\@fontTextBW\fi
            {\overlay{1}}%
  \fi    
  }{%
  \ifDVItoPS
        \par\end{raggedright}%
      \end{minipage}\end{posit@Box}\end{slide@seminar}%
  \else %% DVI -> PDF
        \par\end{raggedright}%
      \end{minipage}\end{posit@Box}\end{slide@seminar}%
        \addtocounter{slide}{-1}%
  \fi
}
\let\slide=\SlideNormal
\let\endslide=\endSlideNormal

\newcommand{\NewSlideStyle}[4][11cm]{%
  \setlength{\slideWidth}{#1}%
  \global\pslongbox{posit@Box}{\rput[#2](#3)}%
  \newslideframe{the#4}{\csname #4\endcsname{##1}}%
  \slidepagestyle{GenericPageStyle}%
  \slideframe{the#4}%
}


% Definition of the bullet for itemize env.
\def\@prosperItemi{{\scriptsize\green%
      \raisebox{2pt}{\ensuremath{\bullet}}}}
\def\@prosperItemii{{\scriptsize\green%
      \raisebox{2pt}{\ensuremath{\bullet}}}}
\def\@prosperItemiii{{\scriptsize\green%
      \raisebox{2pt}{\ensuremath{\bullet}}}}
\newcommand{\myitem}[2]{%
  \ifnum#1=1 
    \gdef\@prosperItemi{#2}%
  \else
    \ifnum#1=2
       \gdef\@prosperItemii{#2}%
    \else
       \ifnum#1=3
          \gdef\@prosperItemiii{#2}%
       \else
          \ClassError{prosper}{Undefined item level}{Available levels: 1,2,3}%
       \fi
    \fi
  \fi
}

\def\@DefMyItem{%
  \global\let\labelitemi=\@prosperItemi
  \global\let\labelitemii=\@prosperItemii
  \global\let\labelitemiii=\@prosperItemiii
}

%% displaying of a logo (to be defined in every style)
\newcommand{\LogoPosition}[1]{\gdef\@PosLogo{#1}}
\def\@PosLogo{\ClassError{prosper}{no default position for positioning the logo given}{Use macro \string\LogoPosition\space in the style file}}
\let\@thelogo\@empty
\newcommand{\Logo}{%
    \@ifnextchar({\Logo@one}{\Logo@two}%
}
\def\Logo@one(#1)#2{%
  \gdef\@PosLogo{#1}%
  \gdef\@thelogo{#2}%
}
\def\Logo@two#1{%
  \gdef\@thelogo{#1}%
}

\newcommand{\PutLogo}{%
  \ifx\@thelogo\@empty\else\rput[lb](\@PosLogo){\@thelogo}\fi
}

\expandafter\ifx\csname realpageref\endcsname\relax%
  \gdef\realpageref#1{\expandafter\real@setref
    \csname r@#1\endcsname\@secondoffive{#1}}
\fi

\newpagestyle{GenericPageStyle}{\hfill }{\ifnoFooter\else\hfill%
 \ifisDraft
 {\@colorFoot\tiny \textbf{\jobname.tex}~--~\@Title~--~\@Author~--~\number\day/\number\month/\number\year~--~\timenow~--~p.~\thepage\ifallPages/\realpageref{last@page}\fi}%
  \else
  \ifshowVersion
 {\@colorFoot\tiny \textbf{\jobname.tex}~--~\@Title~--~\@Author~--~\number\day/\number\month/\number\year~--~\timenow~--~p.~\thepage\ifallPages/\realpageref{last@page}\fi}%
  \else
  {\@colorFoot\tiny \hfill \hfill \hfill \hfill \@cartouche~--~p.~\thepage\ifallPages/\realpageref{last@page}\hfill\fi}%
  \fi\fi\fi
}

\IfFileExists{PPR\Style@chosen.sty}{%
  \RequirePackage{PPR\Style@chosen}%
}{\ClassError{prosper}{unknown Prosper style file: PPR\Style@chosen.sty}{%
  Sorry, no additional help}}


\def\nobabel{%
\let\labelitemi\@ltiORI%
\let\labelitemii\@ltiiORI%
\let\labelitemiii\@ltiiiORI%
\let\labelitemiv\@ltivORI}

% A new itemize environment that does not attempt to justify sentences
\let\Itemize=\itemize
\let\endItemize=\enditemize
\let\orig@item=\item
\renewenvironment{itemize}{\global\let\cur@item=\item 
  \global\let\item=\orig@item%
  \begin{Itemize}\begin{raggedright}}{%
      \end{raggedright}\end{Itemize}\global\let\item=\cur@item}
\let\orig@center=\center
\let\orig@endcenter=\endcenter
\def\center{\global\let\cur@item=\item \global\let\item=\orig@item
  \orig@center}
\def\endcenter{\orig@endcenter \global\let\item=\cur@item}

% itemstep : itemize environment with step-by-step displaying
% Warning: no more than 4 nesting levels allowed!!
\newcounter{item@step}
\newenvironment{itemstep}[1][1]{%
  \begin{Itemize}\begin{raggedright}%
      \ifnum\the\@itemdepth=1%
         \setcounter{item@step}{#1}%
         \addtocounter{item@step}{-1}%
      \fi%
      \def\item{\stepcounter{item@step}%
         \FromSlide{\theitem@step}\orig@item}%
    }{\end{raggedright}\end{Itemize}\ifnum\the\@itemdepth=0\setcounter{item@step}{0}\fi}

%% Displaying the version caption
\newif\ifshowVersion
\showVersionfalse
\newcommand{\displayVersion}{\global\showVersiontrue}

\newcommand{\timenow}{%
  \@tempcnta=\time \divide\@tempcnta by 60 \number\@tempcnta:\multiply
  \@tempcnta by 60 \@tempcntb=\time \advance\@tempcntb by -\@tempcnta
  \ifnum\@tempcntb <10 0\number\@tempcntb\else\number\@tempcntb\fi}

\newcommand{\lastpage@putlabel}{\addtocounter{page}{-1}%
   \immediate\write\@auxout{\string
   \newlabel{last@page}{{}{\theslide}{\relax }{}{}}}%
   \addtocounter{page}{1}}
\AtEndDocument{\ifallPages\clearpage\lastpage@putlabel\fi}

\let\@Subtitle\@empty
\newcommand{\subtitle}[1]{\gdef\@Subtitle{#1}}
\renewcommand{\title}[1]{\gdef\@Title{#1}%
  \ifx\@cartouche\@empty\gdef\@cartouche{#1}\fi}
\renewcommand{\author}[1]{\gdef\@Author{#1}}

\let\@disclaimer\@empty
\newcommand{\disclaimer}[1]{\gdef\@disclaimer{#1}}

\let\@email\@empty
\newcommand{\email}[1]{\gdef\@email{#1}}

\let\@institution\@empty
\newcommand{\institution}[1]{\gdef\@institution{#1}}

\renewcommand{\maketitle}{%
  %% Tests whether the 'french' style from B. Gaulle is loaded
  \expandafter\ifx\csname frenchTeXmods\endcsname\relax%
  \else 
  \PackageWarning{prosper}{This page intentionnally left blank to overcome an incompatibility bug between B. Gaulle 'french' package and the seminar class.}
  \begin{center}
    {\black\small
  \textsf{prosper} class: page intentionnally left blank to overcome an incompatibility bug between B. Gaulle 'french' package and the seminar class.}%
  \end{center}
  \clearpage \setcounter{page}{0}%
  \fi
  \begin{slide}{}%
    \ptsize{10}%
    \begin{center}%
    \@titleSpacing\par
    \normalfont
    {\ifinColor\@fontTitleColor\else\@fontTitleBW\fi\@Title\par}%
    \ifx\@Subtitle\@empty\else

    {\ifinColor\@fontSubtitleColor\else\@fontSubtitleBW\fi\@Subtitle\par}
    \fi

    \vskip.5em
    \@Author

    \ifx\@email\@empty\else
    \vskip-5pt
    {\fontsize{7}{7}\selectfont\texttt{\@email}}
    \fi
    \ifx\@institution\@empty%
    \else
    \vskip.8em
    \@institution
    \fi
    \ifx\@disclaimer\@empty%
    \else
    \vfill \vfill
    {\fontsize{6}{6}\selectfont\textsf{\@disclaimer}}
    \fi
    \vfill
    \end{center}
  \end{slide}
}

%% Overlays environment
\newcounter{overlay@page}
\newenvironment{Overlays}{%
  \global\let\slide\SlideOverlay
  \global\let\endslide\endSlideOverlay
  \setcounter{overlay@page}{\value{slide}}}{%
  \global\let\slide\SlideNormal
  \global\let\endslide\endSlideNormal
  \global\let\@okForBookmark\relax
  \setcounter{slide}{\value{overlay@page}}\stepcounter{slide}}


% fromSlide, onlySlide, untilSlide
% Masks or not the parameter. 
% The stared version does not advance the position pointer and does not
% interpret its argument (to be used for graphics and commands containing
% PStricks links)

% The following uses some pstricks hackery to exclude material from a
% slide. We use pstricks' postscript commands to place material on
% slide one which will never be printed.
\long\def\@on@overlay@one#1{%
  \pst@Verb{(1) BOL}{\ignorespaces #1}\pst@Verb{(\curr@overlay) BOL}}
% [2001/07/16] PMN: The grouping and \ignorespaces around the argument
%   is necessary for downwards compatibility.
%     \ignorespaces is as found in \overlays from seminar and
%     the grouping changes the spacing behavour in TeX.  

\long\def\fromSlide{%
  \@ifstar\fromSlide@E\fromSlide@NE}
\long\def\fromSlide@NE#1{%
  \let\prosper@next\@gobble%
  \ifDVItoPS%
    \ifAccumulate%
      \let\prosper@next\@iden%
    \fi%
  \else%
    \ifnum#1>\value{overlaysCount}%
      \let\prosper@next\@on@overlay@one%
    \else%
      \let\prosper@next\@iden%
    \fi%
  \fi%
  \prosper@next}

\long\def\fromSlide@E#1{%
  \let\prosper@next\@gobble%
  \ifDVItoPS%
    \ifAccumulate%
      \let\prosper@next\@iden%
    \fi%
    \else%
      \ifnum#1>\value{overlaysCount}%
      \else%
        \let\prosper@next\@iden%
      \fi%
    \fi%
  \prosper@next}

\long\def\untilSlide{%
  \@ifstar\untilSlide@E\untilSlide@NE}
\long\def\untilSlide@NE#1{%
  \let\prosper@next\@gobble%
  \ifDVItoPS%
    \ifAccumulate%
      \let\prosper@next\@iden%
    \fi%
  \else%
    \ifnum#1<\value{overlaysCount}%
      \let\prosper@next\@on@overlay@one%
    \else%
      \let\prosper@next\@iden%
    \fi%
  \fi%
  \prosper@next}

\long\def\untilSlide@E#1{%
  \let\prosper@next\@gobble%
  \ifDVItoPS%
    \ifAccumulate%
      \let\prosper@next\@iden%
    \fi%
  \else%
    \ifnum#1<\value{overlaysCount}%
    \else%
      \let\prosper@next\@iden%
    \fi%
  \fi%
  \prosper@next}

\long\def\onlySlide{%
  \@ifstar\onlySlide@E\onlySlide@NE}
\long\def\onlySlide@NE#1{%
  \let\prosper@next\@gobble%
  \ifDVItoPS%
      \ifAccumulate%
        \let\prosper@next\@iden%
      \fi%
  \else%
    \ifnum#1=\value{overlaysCount}%
      \let\prosper@next\@iden%
    \else%
      \let\prosper@next\@on@overlay@one%
    \fi%
  \fi%
  \prosper@next}

\long\def\onlySlide@E#1{%
  \let\prosper@next\@gobble%
  \ifDVItoPS%
    \ifAccumulate%
      \let\prosper@next\@iden%
    \fi%
  \else%
    \ifnum#1=\value{overlaysCount}%
      \let\prosper@next\@iden%
    \fi%
  \fi%
  \prosper@next}

% FromSlide, OnlySlide, UntilSlide
% Masks or not the following material. Advances the position pointer
\newcommand{\FromSlide}[1]{%
\ifDVItoPS%
\else%
  \ifnum#1>\value{overlaysCount}%
  \overlay{1}%
  \fi%
\fi}
\newcommand{\UntilSlide}[1]{%
\ifDVItoPS%
\else%
  \ifnum#1<\value{overlaysCount}%
    \overlay{1}%
  \fi
\fi}
\newcommand{\OnlySlide}[1]{%
\ifDVItoPS%
\else%
  \ifnum#1=\value{overlaysCount}%
  \else%
    \overlay{1}%
  \fi
\fi}

% Overlays. Parameter to state the number of slides composing the overlay
%\def\stepcounter#1{%
%  \ifInOverlays
%    \ifnum\value{overlayscount}<2
%    \else
%      \addtocounter{#1}\@ne
%    \fi
%  \else
%    \addtocounter{#1}\@ne
%  \fi
%  \begingroup
%    \let\@elt\@stpelt
%    \csname cl@#1\endcsname
%  \endgroup
%}

\newif\ifInOverlays
\InOverlaysfalse
\long\def\overlays#1#2{%
  \bgroup  
  \aftergroup\@cleanupOverlay
  \global\InOverlaystrue
  \setcounter{limitOverlays}{#1}%
  \setcounter{overlaysCount}{1}%
  \ifDVItoPS
    #2
  \else
    \begin{Overlays}%
    \bgroup  
    \loop 
      \@everyOverlay
      #2
    \ifnum\value{overlaysCount}<\value{limitOverlays}%
      \stepcounter{overlaysCount}%
    \repeat
    \egroup
    \end{Overlays}%
  \fi
  \egroup
}

\def\@cleanupOverlay{%
  \global\InOverlaysfalse
  \setcounter{nbAffected}{0}
}

\def\@listctr{}
\AtBeginDocument{%
  \def\@pdfcreator{LaTeX with hyperref and prosper packages}%
  {\begingroup \escapechar\m@ne\xdef\@gtempa{{\string\reset@color}}\endgroup
    \expandafter\@ifundefined\@gtempa\relax{%
      \global\let\orig@reset@color=\reset@color
      \gdef\reset@color{\orig@reset@color\pst@endcolor}}}%
 \let\Label=\label
 \def\label#1{%
    \ifInOverlays
      \ifnum\value{overlaysCount}=1
        \Label{#1}
      \fi
    \else
      \Label{#1}
    \fi
 }

 % For amsmath environments
 \let\Label@in@display=\label@in@display
 \def\label@in@display#1{%
   \ifInOverlays
     \ifnum\value{overlaysCount}=1
       \Label@in@display{#1}
     \fi
   \else
     \Label@in@display{#1}%
   \fi
}%

  \let\overlay@loop=\relax
  \let\stepcounter=\stepcounter
% Macro \overlay taken from semlayer.sty
\def\overlay#1{%
  \ifoverlays\ifx\@overlay\relax\nooverlays@err\fi\fi
  \ifslide
    \@tempcnta=#1
    \@testfalse
    \ifnum\@tempcnta>-1 \ifnum\@tempcnta<10 \@testtrue\fi\fi
    \if@test
      \c@overlay=\@tempcnta
      \ifnum\c@overlay=0
        \advance\c@slide by -1
        \refstepcounter{slide}%
      \else
        \advance\c@overlay by -1
        \refstepcounter{overlay}%
        \ifoverlays
          \xdef\overlay@list{\overlay@list,\the\c@overlay}%
        \fi
      \fi
      \ifoverlays
        \@overlay{\the\c@overlay}%
      \fi
    \else
      \@seminarerr{\string\overlay\space argument must be
        an integer between 0 and 9}\@eha
    \fi
  \else
    \@seminarerr{\string\overlay\space can only be used
      in slide environments}\@eha
  \fi
  \ignorespaces}%
} % AtBeginDocument

\let\@everyOverlay\relax
%% Macros to be executed before each overlayed slide
\def\everyOverlay#1{\gdef\@everyOverlay{#1}}

\newcounter{nbAffected}
\newcounter{savecountAffected}
\def\saveCtr#1{%
  \stepcounter{nbAffected}
  \expandafter\@saveCtr\the\csname c@#1\endcsname{#1}
}
\def\@saveCtr#1#2{
  \expandafter\gdef\csname @sac\the\c@nbAffected\endcsname{\fromSlide*{2}{\setcounter{#2}{#1}}}
}

\def\@prepareResetCounters{%
  \ifnum\value{nbAffected}>0 
     \setcounter{savecountAffected}{1}
     \loop
       \csname @sac\the\c@savecountAffected\endcsname
     \ifnum\value{savecountAffected}<\value{nbAffected}
       \stepcounter{savecountAffected}
     \repeat
  \fi
}


% \part
\let\part=\relax
\newcommand{\part}[2][\@defaultTransition]{%
  \begin{slide}[#1]{}%
    \vspace*{1.5cm}\@addBookmarkOnSlide{0}{#2}%
    \begin{center}%
      \fontTitle{#2}%
    \end{center}
  \end{slide}}

% Uses #1 if PDF option, otherwise #2
% [7/13/01] PMN: More robust implementation as it does not scan the
% arguments for \if.
\newcommand{\PDForPS}{%
  \ifDVItoPS
    \let\prosper@next\@secondoftwo
  \else
    \let\prosper@next\@firstoftwo
  \fi
  \prosper@next}
% #1 appears on the slide only on PS mode
\newcommand{\onlyInPS}{%
  \ifDVItoPS 
    \let\prosper@next\@iden
   \else
    \let\prosper@next\@gobble
  \fi
  \prosper@next}
% #1 appears on the slide only on PDF mode
\newcommand{\onlyInPDF}{
  \ifDVItoPS
    \let\prosper@next\@gobble
  \else
    \let\prosper@next\@iden
  \fi
  \prosper@next}

% To be used in conjunction with babel/french to allow ones own labels
\def\NoFrenchBabelItemize{%
\AtBeginDocument{%
\renewenvironment{itemize}%
{\ifnum \@itemdepth >\thr@@\@toodeep\else
    \advance\@itemdepth\@ne
    \edef\@itemitem{labelitem\romannumeral\the\@itemdepth}%
    \expandafter
    \list
      \csname\@itemitem\endcsname%
    {\let\@trivlist\@trivlistORI
      \settowidth{\labelwidth}{\textendash}%
      \setlength{\leftmargin}{\labelwidth}%
      \addtolength{\leftmargin}{\labelsep}%
      \ifnum\@listdepth=0
      \setlength{\itemindent}{\parindent}%
      \else
      \addtolength{\leftmargin}{\parindent}%
      \fi
      \setlength{\itemsep}{\z@}%
      \setlength{\parsep}{\z@}%
      \setlength{\topsep}{\z@}%
      \setlength{\partopsep}{\z@}%
      \addtolength{\topsep}{-\parskip}%
      \addtolength{\partopsep}{\parskip}%
      }%
    \fi}%
 {\endlist}%
}}

\ptsize{14}

\endinput

%%% Local Variables: 
%%% mode: latex
%%% TeX-master: t
%%% End: 
