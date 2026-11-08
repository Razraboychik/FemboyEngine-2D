# Documents

The document subsystem is a VFS-backed, fixed-slot reader for plain text
in-game manuals, books and help screens.

## Text

`DocumentSystem` opens `.txt`/`.text` files through the VFS. UTF-8 is required; a
BOM is stripped and CRLF is normalised to LF. The text is copied once into a cold
slot, split into line starts and paginated by `LinesPerPage` (32 lines by
default).

- `DocumentHandle` is a slot + generation; a reused slot invalidates older handles.
- `GetInfo()` returns page/line count, byte size and the first non-empty line as
  the title.
- `PageText()` copies the page's UTF-8 bytes; the destination may be shorter than
  the page, `OutBytes` reports the stored size.
- `FindPage()` returns the first page containing a byte substring.
- Open/Close belong to the OS main thread (`RequireMainThread`); `PageText` only
  reads immutable slot data.

The reader owns plain text only. PDFs, DjVu and other binary page formats are not
part of `DocumentSystem`: a fixed-layout viewer would be a separate optional
system with its own license/ABI boundary. Rich documents are handled by the
streaming [book format](books.md), not by this class.
