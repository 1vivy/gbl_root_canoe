use abl_tzmap::evidence::{lookup, parse_table, tables, EvidenceError};
use abl_tzmap::manifest::Semantic;

const PRIMARY: [u8; 32] = [
    0xe2, 0xdb, 0xe1, 0xb8, 0x8f, 0x28, 0x75, 0x1b, 0x76, 0x22, 0x49, 0x98, 0x6f, 0xe6, 0xb5, 0x1c,
    0xb3, 0xe3, 0xbe, 0x53, 0x25, 0x7f, 0x0d, 0xd2, 0xd6, 0x2f, 0x12, 0xfd, 0xb9, 0x01, 0x54, 0x97,
];

/// Every committed table must parse; a transcription slip is a test failure,
/// not a silent behaviour change.
#[test]
fn committed_tables_parse() {
    let parsed = tables().expect("committed evidence tables parse");
    assert_eq!(parsed.len(), 3);
    for table in &parsed {
        assert!(!table.commands.is_empty(), "{} has no commands", table.name);
    }
}

/// Recorded request lengths must agree with the compiled KeyMaster structures,
/// otherwise the firmware would refuse every rewrite with a size mismatch.
#[test]
fn recorded_lengths_match_the_compiled_layouts() {
    for table in tables().expect("committed evidence tables parse") {
        for record in &table.commands {
            let expected = match record.semantic {
                Semantic::SetRot => Some(44),
                Semantic::SetBootstate => Some(64),
                Semantic::SetVbh => Some(36),
                _ => None,
            };
            if let Some(expected) = expected {
                assert_eq!(
                    record.request_bytes, expected,
                    "{} command 0x{:03x}",
                    table.name, record.command
                );
            }
        }
    }
}

/// 0x207 is only a failure-log argument on these builds and 0x202/0x203 were
/// never observed as real sends, so no table may claim a length for them.
#[test]
fn unobserved_commands_are_absent_from_evidence() {
    for table in tables().expect("committed evidence tables parse") {
        for command in [0x202u16, 0x203, 0x207] {
            assert!(
                !table.commands.iter().any(|record| record.command == command),
                "{} must not claim command 0x{command:03x}",
                table.name
            );
        }
    }
}

#[test]
fn tables_are_addressed_by_distinct_digests() {
    let parsed = tables().expect("committed evidence tables parse");
    let mut digests = parsed.iter().map(|table| table.digest).collect::<Vec<_>>();
    digests.sort_unstable();
    let count = digests.len();
    digests.dedup();
    assert_eq!(digests.len(), count, "evidence digests must be unique");
}

#[test]
fn lookup_resolves_a_recorded_digest_and_misses_others() {
    let found = lookup(&PRIMARY).expect("lookup parses").expect("primary digest is recorded");
    assert_eq!(found.name, "cph2767-macan-16.0.9.401");
    assert_eq!(found.commands.len(), 6);
    let milestone = found.commands.iter().find(|record| record.command == 0x204).expect("primary evidence records milestone");
    assert_eq!(milestone.semantic, Semantic::Milestone);
    assert_eq!(milestone.request_bytes, 0);
}

#[test]
fn rejects_a_table_without_a_digest() {
    let body = "command=0x201 size=44 semantic=set_rot occurrences=1\n";
    assert_eq!(parse_table("t", body), Err(EvidenceError::MissingDigest { table: "t" }));
}

#[test]
fn rejects_malformed_fields_and_duplicates() {
    let header = "sha256=".to_string() + &"ab".repeat(32) + "\n";
    let cases: [(&str, &str); 6] = [
        ("command=0x201 size=44 semantic=set_rot", "missing occurrences"),
        ("command=201 size=44 semantic=set_rot occurrences=1", "command must be 0x-prefixed"),
        ("command=0x201 size=x semantic=set_rot occurrences=1", "size is not a u16"),
        ("command=0x201 size=44 semantic=nope occurrences=1", "unknown semantic token"),
        ("command=0x201 size=44 semantic=set_rot occurrences=1 extra=1", "unrecognised field"),
        ("command=0x000 size=44 semantic=set_rot occurrences=1", "command id zero"),
    ];
    for (line, reason) in cases {
        let body = header.clone() + line + "\n";
        assert_eq!(
            parse_table("t", &body),
            Err(EvidenceError::Malformed { table: "t", line: 2, reason }),
            "case: {line}"
        );
    }
    let duplicate = header.clone()
        + "command=0x201 size=44 semantic=set_rot occurrences=1\n"
        + "command=0x201 size=44 semantic=set_rot occurrences=1\n";
    assert_eq!(
        parse_table("t", &duplicate),
        Err(EvidenceError::DuplicateCommand { table: "t", command: 0x201 })
    );
    assert_eq!(
        parse_table("t", "sha256=abcd\n"),
        Err(EvidenceError::Malformed {
            table: "t",
            line: 1,
            reason: "sha256 must be 64 lowercase hex digits",
        })
    );
}

/// Comments and blank lines are ignored so tables can carry their provenance.
#[test]
fn comments_and_blank_lines_are_ignored() {
    let body = format!(
        "# provenance\n\nsha256={}\n\n# the only send\ncommand=0x211 size=36 semantic=set_vbh occurrences=1 # trailing\n",
        "cd".repeat(32)
    );
    let table = parse_table("t", &body).expect("table parses");
    assert_eq!(table.commands.len(), 1);
    assert_eq!(table.commands[0].command, 0x211);
    assert_eq!(table.commands[0].request_bytes, 36);
}
