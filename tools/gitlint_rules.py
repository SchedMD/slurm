from itertools import groupby
import re
from dataclasses import dataclass
from enum import Enum
from typing import Any

from gitlint.rules import CommitRule, RuleViolation

multiline_re = re.compile(r"^\s")
allspace_re = re.compile(r"^\s+$")
trailer_re = re.compile(r"^(\w+):")


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
    tag: TrailerType
    content: str
    trailer_re = re.compile(r"(.+?):(.+)")
    trailer_line_re = re.compile(r"\s+.*")

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
            return None, lines
        group = [top]
        for i, peek in enumerate(lines[1:], start=1):
            # if the next line is the start of a trailer, this group is done
            if Trailer.is_trailer(peek):
                break
            if Trailer.is_trailer_multiline(peek):
                group.append(peek)
            else:
                break
        return (cls(tag, "\n".join(group)), lines[i:])

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
        sections = [
            list(paragraph)
            for k, paragraph in groupby(
                split_trailers_and_body(commit.message.body), lambda s: not s
            )
            if not k
        ]
        # print(*sections, sep="\n")
        trailer_found = False
        for i, paragraph in enumerate(sections):
            if len(paragraph) == 0:
                print(f"zero length paragraph {i}? '{paragraph}'")
                continue
            if not trailer_found:
                trailer_found = isinstance(paragraph[0], Trailer)
            if trailer_found:
                non_trailer = next(
                    (seg for seg in paragraph if not isinstance(seg, Trailer)), None
                )
                if non_trailer is not None:
                    msg = f"'{non_trailer}' is not a valid trailer"
                    return [RuleViolation(self.id, msg)]
            else:
                trailer = next(
                    (seg for seg in paragraph if isinstance(seg, Trailer)), None
                )
                if trailer is not None:
                    msg = "Trailers must be separated from the body by an empty line"
                    return [RuleViolation(self.id, msg)]
