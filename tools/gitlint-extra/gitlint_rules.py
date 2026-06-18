import re
from dataclasses import dataclass
from enum import Enum
from itertools import groupby, takewhile, chain
from typing import Any

from gitlint.rules import CommitRule, RuleViolation


class ExtendedEnum(Enum):
    @classmethod
    def values(cls) -> dict[str, str]:
        return dict(map(lambda ty: (ty.value, ty.name), cls))

    @classmethod
    def case_values(cls) -> dict[str, str]:
        return {
            opt: name
            for val, name in cls.values().items()
            for opt in [val, val.upper(), val.lower(), val.capitalize()]
        }


class TrailerType(ExtendedEnum):
    CHANGELOG = "Changelog"
    CHERRY_PICK = "Cherry-picked"
    CID = "CID"
    CO_AUTHOR = "Co-authored-by"
    ISSUE = "Issue"
    SIGNED_OFF = "Signed-off-by"
    TICKET = "Ticket"

    @classmethod
    def from_str(cls, label: str) -> Any:
        """get enum value from string label"""
        typ = getattr(cls, label, None)
        if typ is None:
            val = cls.case_values().get(label, None)
            if val is None:
                return None
            typ = getattr(cls, val, None)
        return typ


@dataclass
class Trailer:
    tag: TrailerType | str
    lines: list[str]
    trailer_re = re.compile(r"([\w\-]+?): (\w.+)")
    trailer_line_re = re.compile(r"\s+.*")

    def __repr__(self):
        return self.content

    @property
    def content(self):
        return "\n".join(self.lines)

    @classmethod
    def from_lines(cls, lines: list[str]) -> tuple[Any, list[str]]:
        """make a trailer from a list of lines, return the excess
        Return None if not a valid Trailer"""
        top = lines[0]
        m = cls.trailer_re.match(top)
        if m is None:
            return None, lines
        tag = TrailerType.from_str(m[1])
        if tag is None:
            tag = m[1]
        group = [top]
        i = 1
        for i, peek in enumerate(lines[1:], start=i):
            # if the next line is the start of a trailer, this group is done
            if Trailer.is_trailer(peek):
                break
            if Trailer.is_trailer_multiline(peek):
                group.append(peek)
            else:
                break
        else:
            # if for ended on multiline, increment i to consume last line
            i += 1
        return (cls(tag, group), lines[i:])

    @classmethod
    def pull_non_trailer(cls, lines: list[str]) -> tuple[str, list[str]]:
        """Pull off any non-trailer lines, return the rest.
        The first line could look like a trailer, but pull it anyway"""
        if lines[0] == "":
            return ("", lines[1:])
        group = [lines[0]]
        i = 1
        for i, line in enumerate(lines[1:], start=i):
            # if the next line is the start of a trailer, this group is done
            if Trailer.is_trailer(line) or line == "":
                break
            group.append(line)
        return ("\n".join(group), lines[i:])

    @classmethod
    def is_trailer(cls, text: str) -> bool:
        return cls.trailer_re.match(text)

    @classmethod
    def is_trailer_multiline(cls, line: str) -> bool:
        return cls.trailer_line_re.match(line)

    @classmethod
    def check_valid_trailer(cls, maybe_trailer):
        pass

    def is_known(self):
        return isinstance(self.tag, TrailerType)


def split_trailers_and_body(body: list[str]):
    # print(body)
    lines = body.copy()
    while lines:
        trailer, lines = Trailer.from_lines(lines)
        if trailer is not None:
            yield trailer
        else:
            section, lines = Trailer.pull_non_trailer(lines)
            if section is not None:
                yield section


class TrailerValidation(CommitRule):
    """Enforce that multiline changelog trailer starts with a blank space."""

    # A rule MUST have a human friendly name
    name = "changelog-trailer"

    # A rule MUST have a *unique* id
    # We recommend starting with UC (for User-defined Commit-rule).
    id = "UC100"

    def validate(self, commit):
        violations = []
        sections = [
            list(paragraph)
            for k, paragraph in groupby(
                split_trailers_and_body(commit.message.body), lambda s: not s
            )
            if not k
        ]
        if len(sections) == 0:
            # empty body
            return violations
        # check trailer section
        trailer_section = sections[-1]

        if len(trailer_section) == 0:
            # TODO error here? This means the body is empty
            # or what if there are multiple newlines at the end?
            print("empty body?")
            return violations

        nontrailer = [line for line in trailer_section if not isinstance(line, Trailer)]
        # check for mis-formatted trailers in last paragraph
        violations.extend(
            RuleViolation(self.id, f"Misformatted trailer: '{line}'")
            for line in nontrailer
            if any(line.startswith(tag) for tag in TrailerType.case_values().keys())
        )

        # check for no trailers or merged body and trailers
        if not isinstance(trailer_section[0], Trailer):
            if isinstance(trailer_section[-1], Trailer):
                violations.append(
                    RuleViolation(
                        self.id,
                        "Newline required between trailer section and main commit message body.",
                    )
                )
                return violations
            # No trailers at all!
            return violations

        # check for non-trailer lines in the trailer section
        violations.extend(
            RuleViolation(
                self.id,
                f"Trailer section should include only trailers: '{line.strip()}' is not a trailer. Multi-line trailer might need indents?",
            )
            for line in nontrailer
        )

        # check for unknown trailers in trailer section
        trailers = [tr for tr in trailer_section if isinstance(tr, Trailer)]
        unknown_trailers = [tr for tr in trailers if not tr.is_known()]
        violations.extend(
            RuleViolation(
                self.id,
                f"Trailer section has unknown trailer: '{tr}'",
            )
            for tr in unknown_trailers
        )

        # check for trailers at end of last body section
        if len(sections) > 1:
            last_body_section = sections[-2]
            trailers_in_body = list(
                takewhile(
                    lambda line: isinstance(line, Trailer), reversed(last_body_section)
                )
            )
            violations.extend(
                RuleViolation(self.id, f"Trailer should be in trailer section: '{tr}'")
                for tr in trailers_in_body
            )

        # check for valid trailers outside the trailer section
        violations.extend(
            RuleViolation(self.id, f"Valid trailer outside trailer section: '{line}'")
            for line in chain.from_iterable(sections[:-1])
            if isinstance(line, Trailer) and line.is_known()
        )

        # check line length of trailers
        violations.extend(
            RuleViolation(
                self.id,
                f"Trailer line too long, must be less than 76 characters. '{line}'",
            )
            for tr in trailers
            for line in tr.lines
            if len(line) > 76
        )
        return violations
