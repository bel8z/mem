const std = @import("std");
const mem = std.mem;
const assert = std.debug.assert;

pub fn Buf(comptime T: type) type {
    return struct {
        items: []T = &[_]T{},
        capacity: usize = 0,

        pub const Error = error{Full} || mem.Allocator.Error;

        const Self = @This();

        pub fn init(buf: []T) Self {
            return Self{
                .items = buf[0..0],
                .capacity = buf.len,
            };
        }

        pub inline fn len(self: *const Self) usize {
            return self.items.len;
        }

        pub inline fn last(self: *Self) ?*T {
            return if (self.items.len > 0) &self.items[self.items.len - 1] else null;
        }

        pub fn clear(self: *Self) void {
            self.items.len = 0;
        }

        pub fn pop(self: *Self) void {
            if (self.items.len > 0) self.items.len -= 1;
        }

        /// Removes the element at the specified index and returns it.
        /// The empty slot is filled from the end of the list.
        /// Invalidates pointers to last element.
        /// This operation is O(1).
        pub fn swapRemove(self: *Self, pos: usize) T {
            assert(pos < self.items.len);

            const last_ptr = self.last() orelse unreachable;
            const item = self.items[pos];

            self.items[pos] = last_ptr.*;
            last_ptr.* = undefined;
            self.items.len -= 1;

            return item;
        }

        /// Remove the element at index `pos` from the list and return its value.
        /// Asserts the array has at least one item. Invalidates pointers to
        /// last element.
        /// This operation is O(N).
        pub fn orderedRemove(self: *Self, pos: usize) T {
            assert(pos >= 0 and pos < self.items.len);

            const item = self.items[pos];
            const next_len = self.items.len - 1;

            std.mem.copy(T, self.items[pos..next_len], self.items[pos + 1 .. self.items.len]);
            self.items[next_len] = undefined;
            self.items.len = next_len;

            return item;
        }

        pub inline fn append(self: *Self, item: T) Error!void {
            if (self.items.len == self.capacity) return error.Full;
            self.items.len += 1;
            self.items[self.items.len - 1] = item;
        }

        pub fn appendAlloc(self: *Self, item: T, allocator: mem.Allocator) Error!void {
            try self.ensureUnusedCapacity(1, allocator);
            try self.append(item);
        }

        pub fn extend(self: *Self, amount: usize) Error![]T {
            const avail = self.capacity - self.items.len;
            if (avail < amount) return error.Full;

            const res = self.items[self.items.len][0..amount];
            self.items.len += amount;

            return res;
        }

        pub fn extendAlloc(self: *Self, amount: usize, allocator: mem.Allocator) Error![]T {
            try self.ensureUnusedCapacity(amount, allocator);
            return self.extend(self, amount);
        }

        pub fn resize(self: *Self, new_len: usize) Error!void {
            if (new_len > self.capacity) return error.Full;
            if (new_len < self.items.len) std.mem.set(T, self.items[new_len..], undefined);
            self.items.len = new_len;
        }

        pub fn resizeAlloc(self: *Self, new_len: usize, allocator: mem.Allocator) Error!void {
            try self.ensureTotalCapacity(new_len, allocator);
            try self.resize(new_len);
        }

        pub fn ensureUnusedCapacity(self: *Self, amount: usize, allocator: mem.Allocator) Error!void {
            try ensureTotalCapacity(self, self.items.len + amount, allocator);
        }

        pub fn ensureTotalCapacity(self: *Self, request: usize, allocator: mem.Allocator) Error!void {
            if (self.capacity > request) return;

            const next_cap = if (self.capacity == 0) 1 else self.capacity;

            while (request > next_cap) {
                next_cap *= 2;
            }

            self.items = try allocator.realloc(self.items, next_cap);
        }
    };
}

test "Basics" {
    const testing = std.testing;
    var storage: [1024]usize = undefined;
    var buf = Buf(usize).init(&storage);

    try testing.expectEqual(storage.len, buf.capacity);
    try testing.expectEqual(@as(usize, 0), buf.len());

    try buf.append(0);
    try buf.append(1);
    try buf.append(2);

    try testing.expectEqual(3, buf.len());

    for (buf.items) |item, index| {
        try testing.expectEqual(index, item);
    }

    buf.pop();
    try testing.expectEqual(2, buf.len());

    buf.clear();
    try testing.expectEqual(0, buf.len());
}

test "swapRemove" {
    const testing = std.testing;
    var storage: [1024]usize = undefined;
    var buf = Buf(usize).init(&storage);

    try buf.append(0);
    try buf.append(7);
    try buf.append(1);
    try buf.append(2);

    try testing.expectEqual(7, buf.swapRemove(1));
    try testing.expectEqual(3, buf.len());
    try testing.expectEqual(0, buf.items[0]);
    try testing.expectEqual(2, buf.items[1]);
    try testing.expectEqual(1, buf.items[2]);
}

test "orderedRemove" {
    const testing = std.testing;
    var storage: [1024]usize = undefined;
    var buf = Buf(usize).init(&storage);

    try buf.append(0);
    try buf.append(7);
    try buf.append(1);
    try buf.append(2);

    try testing.expectEqual(7, buf.orderedRemove(1));
    try testing.expectEqual(3, buf.len());
    for (buf.items) |item, index| {
        try testing.expectEqual(index, item);
    }
}
