#!/usr/bin/env python3
"""Host-side tests for the fixed-slot minute ring log ordering.

The firmware stores minute aggregates in fixed slots and advances a write
index. These tests verify the expected read order before the embedded code is
changed, so wraparound mistakes are caught outside the board too.
"""

from dataclasses import dataclass


@dataclass
class Ring:
    capacity: int
    write_index: int = 0
    count: int = 0

    def append(self, value: int) -> None:
        self.slots[self.write_index] = value
        self.write_index = (self.write_index + 1) % self.capacity
        if self.count < self.capacity:
            self.count += 1

    def rows(self, limit: int = 0) -> list[int]:
        start = (self.write_index + self.capacity - self.count) % self.capacity
        out = [self.slots[(start + i) % self.capacity] for i in range(self.count)]
        if limit and len(out) > limit:
            out = out[-limit:]
        return out

    def __post_init__(self) -> None:
        self.slots = [None] * self.capacity


def test_not_full_order() -> None:
    ring = Ring(5)
    for value in [10, 11, 12]:
        ring.append(value)
    assert ring.rows() == [10, 11, 12]


def test_wrap_keeps_newest_in_order() -> None:
    ring = Ring(5)
    for value in range(10):
        ring.append(value)
    assert ring.rows() == [5, 6, 7, 8, 9]


def test_limit_returns_tail() -> None:
    ring = Ring(8)
    for value in range(6):
        ring.append(value)
    assert ring.rows(limit=3) == [3, 4, 5]


def test_limit_after_wrap() -> None:
    ring = Ring(4)
    for value in range(7):
        ring.append(value)
    assert ring.rows(limit=2) == [5, 6]


def test_zero_limit_means_all_rows() -> None:
    ring = Ring(3)
    for value in range(5):
        ring.append(value)
    assert ring.rows(limit=0) == [2, 3, 4]


def main() -> None:
    tests = [
        test_not_full_order,
        test_wrap_keeps_newest_in_order,
        test_limit_returns_tail,
        test_limit_after_wrap,
        test_zero_limit_means_all_rows,
    ]
    for test in tests:
        test()
    print(f"[PASS] {len(tests)} ring log tests")


if __name__ == "__main__":
    main()
