#include "xmlparser.hpp"

#include <windows.h>

extern "C" void *__cdecl xml_memset(void *destination, int value,
                                    unsigned __int64 size) {
  unsigned char *bytes = static_cast<unsigned char *>(destination);

  for (unsigned __int64 i = 0; i < size; ++i)
    bytes[i] = static_cast<unsigned char>(value);

  return destination;
}

namespace xml {

namespace {

struct Arena {
  unsigned char *memory;
  size_type capacity;
  size_type used;
};

struct Parser {
  const char *source;
  size_type size;
  size_type position;
  size_type line;
  size_type column;
  Arena *arena;
  Error *error;
};

bool same(Slice left, Slice right) {
  if (left.size != right.size)
    return false;

  for (size_type i = 0; i < left.size; ++i)
    if (left.data[i] != right.data[i])
      return false;

  return true;
}

size_type length(const char *text) {
  size_type result = 0;
  if (!text)
    return 0;
  while (text[result])
    ++result;
  return result;
}

void fail(Parser *parser, ParseCode code) {
  if (parser->error && parser->error->code == ParseCode::Ok) {
    parser->error->code = code;
    parser->error->offset = parser->position;
    parser->error->line = parser->line;
    parser->error->column = parser->column;
  }
}

char peek(const Parser *parser) {
  return parser->position < parser->size ? parser->source[parser->position] : 0;
}

char take(Parser *parser) {
  char result = peek(parser);
  if (!result)
    return 0;
  ++parser->position;
  if (result == '\n') {
    ++parser->line;
    parser->column = 1;
  } else
    ++parser->column;
  return result;
}

bool starts(const Parser *parser, const char *text) {
  size_type count = length(text);
  if (parser->position + count > parser->size)
    return false;
  for (size_type i = 0; i < count; ++i)
    if (parser->source[parser->position + i] != text[i])
      return false;
  return true;
}

bool whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
void skip_space(Parser *parser) {
  while (whitespace(peek(parser)))
    take(parser);
}

bool name_start(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
         c == ':';
}
bool name_char(char c) {
  return name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

Slice parse_name(Parser *parser) {
  size_type begin = parser->position;
  if (!name_start(peek(parser))) {
    fail(parser, ParseCode::InvalidName);
    return {0, 0};
  }
  take(parser);
  while (name_char(peek(parser)))
    take(parser);
  return {parser->source + begin, parser->position - begin};
}

void *allocate(Arena *arena, size_type amount, size_type alignment) {
  size_type mask = alignment - 1;
  size_type start = (arena->used + mask) & ~mask;
  if (start > arena->capacity || amount > arena->capacity - start)
    return 0;
  void *result = arena->memory + start;
  arena->used = start + amount;
  volatile unsigned char *bytes = static_cast<volatile unsigned char *>(result);
  for (size_type i = 0; i < amount; ++i)
    bytes[i] = 0;
  return result;
}

Node *new_node(Parser *parser, NodeType type) {
  Node *node =
      static_cast<Node *>(allocate(parser->arena, sizeof(Node), alignof(Node)));
  if (!node) {
    fail(parser, ParseCode::OutOfMemory);
    return 0;
  }
  node->type = type;
  return node;
}

Attribute *new_attribute(Parser *parser) {
  Attribute *result = static_cast<Attribute *>(
      allocate(parser->arena, sizeof(Attribute), alignof(Attribute)));
  if (!result)
    fail(parser, ParseCode::OutOfMemory);
  return result;
}

Slice quoted(Parser *parser) {
  char quote = peek(parser);
  if (quote != '\'' && quote != '"') {
    fail(parser, ParseCode::InvalidSyntax);
    return {0, 0};
  }
  take(parser);
  size_type begin = parser->position;
  while (peek(parser) && peek(parser) != quote) {
    if (peek(parser) == '<')
      fail(parser, ParseCode::InvalidSyntax);
    take(parser);
  }
  if (!peek(parser)) {
    fail(parser, ParseCode::UnexpectedEnd);
    return {0, 0};
  }
  Slice result = {parser->source + begin, parser->position - begin};
  take(parser);
  return result;
}

static Node *element(Parser *parser);

Node *text(Parser *parser) {
  size_type begin = parser->position;
  while (peek(parser) && peek(parser) != '<')
    take(parser);
  Node *node = new_node(parser, NodeType::Text);
  if (node)
    node->value = {parser->source + begin, parser->position - begin};
  return node;
}

Node *marked(Parser *parser, const char *opener, const char *closer,
             NodeType type) {
  size_type opener_size = length(opener);
  for (size_type i = 0; i < opener_size; ++i)
    take(parser);
  size_type begin = parser->position;
  while (!starts(parser, closer)) {
    if (!peek(parser)) {
      fail(parser, ParseCode::UnexpectedEnd);
      return 0;
    }
    take(parser);
  }
  Node *node = new_node(parser, type);
  if (node)
    node->value = {parser->source + begin, parser->position - begin};
  for (size_type i = 0, n = length(closer); i < n; ++i)
    take(parser);
  return node;
}

Node *processing_instruction(Parser *parser) {
  take(parser);
  take(parser);
  Slice target = parse_name(parser);
  if (!target.data)
    return 0;
  size_type begin = parser->position;
  while (!starts(parser, "?>")) {
    if (!peek(parser)) {
      fail(parser, ParseCode::UnexpectedEnd);
      return 0;
    }
    take(parser);
  }
  Node *node = new_node(parser, NodeType::ProcessingInstruction);
  if (node) {
    node->name = target;
    node->value = {parser->source + begin, parser->position - begin};
  }
  take(parser);
  take(parser);
  return node;
}

Node *element(Parser *parser) {
  if (take(parser) != '<') {
    fail(parser, ParseCode::InvalidSyntax);
    return 0;
  }
  Node *node = new_node(parser, NodeType::Element);
  if (!node)
    return 0;
  node->name = parse_name(parser);
  while (true) {
    skip_space(parser);
    if (starts(parser, "/>")) {
      take(parser);
      take(parser);
      return node;
    }
    if (take(parser) == '>')
      break;
    --parser->position;
    --parser->column;
    Attribute *attr = new_attribute(parser);
    if (!attr)
      return 0;
    attr->name = parse_name(parser);
    skip_space(parser);
    if (take(parser) != '=') {
      fail(parser, ParseCode::InvalidSyntax);
      return 0;
    }
    skip_space(parser);
    attr->value = quoted(parser);
    for (Attribute *old = node->first_attribute; old; old = old->next)
      if (same(old->name, attr->name)) {
        fail(parser, ParseCode::DuplicateAttribute);
        return 0;
      }
    attr->next = node->first_attribute;
    node->first_attribute = attr;
  }
  while (true) {
    if (!peek(parser)) {
      fail(parser, ParseCode::UnexpectedEnd);
      return 0;
    }
    if (starts(parser, "</")) {
      take(parser);
      take(parser);
      Slice closing = parse_name(parser);
      skip_space(parser);
      if (!same(closing, node->name)) {
        fail(parser, ParseCode::MismatchedTag);
        return 0;
      }
      if (take(parser) != '>') {
        fail(parser, ParseCode::InvalidSyntax);
        return 0;
      }
      return node;
    }
    Node *child_node = 0;
    if (starts(parser, "<!--"))
      child_node = marked(parser, "<!--", "-->", NodeType::Comment);
    else if (starts(parser, "<![CDATA["))
      child_node = marked(parser, "<![CDATA[", "]]>", NodeType::CData);
    else if (starts(parser, "<?"))
      child_node = processing_instruction(parser);
    else if (peek(parser) == '<')
      child_node = element(parser);
    else
      child_node = text(parser);
    if (!child_node)
      return 0;
    if (node->last_child)
      node->last_child->next_sibling = child_node;
    else
      node->first_child = child_node;
    node->last_child = child_node;
  }
}

bool parse_impl(Document *document, const char *source, size_type size,
                Error *error, Arena *arena) {
  if (error) {
    error->code = ParseCode::Ok;
    error->offset = 0;
    error->line = 1;
    error->column = 1;
  }
  Parser parser = {source, size, 0, 1, 1, arena, error};
  if (size >= 3 && source[0] == static_cast<char>(0xEF) &&
      source[1] == static_cast<char>(0xBB) &&
      source[2] == static_cast<char>(0xBF)) {
    take(&parser);
    take(&parser);
    take(&parser);
  }
  skip_space(&parser);
  if (starts(&parser, "<?xml")) {
    Node *declaration = processing_instruction(&parser);
    if (!declaration)
      return false;
  }
  skip_space(&parser);
  if (starts(&parser, "<!DOCTYPE")) {
    while (peek(&parser) && take(&parser) != '>') {
    }
    skip_space(&parser);
  }
  document->root_node = element(&parser);
  if (!document->root_node)
    return false;
  skip_space(&parser);
  if (peek(&parser)) {
    fail(&parser, ParseCode::InvalidSyntax);
    return false;
  }
  return !error || error->code == ParseCode::Ok;
}

} // namespace

bool equal(Slice left, const char *right) {
  return same(left, {right, length(right)});
}

bool parse(Document *document, const char *source, size_type size,
           Error *error) {
  if (!document || !source)
    return false;
  document->root_node = 0;
  document->arena = 0;
  size_type capacity = size * 2 + 4096;
  Arena *arena = static_cast<Arena *>(
      HeapAlloc(GetProcessHeap(), 0, sizeof(Arena) + capacity));
  if (!arena) {
    if (error)
      error->code = ParseCode::OutOfMemory;
    return false;
  }
  arena->memory = reinterpret_cast<unsigned char *>(arena + 1);
  arena->capacity = capacity;
  arena->used = 0;
  document->arena = arena;
  if (!parse_impl(document, source, size, error, arena)) {
    destroy(document);
    return false;
  }
  return true;
}

bool parse_file(Document *document, const char *path, Error *error) {
  if (!document || !path)
    return false;
  HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  if (file == INVALID_HANDLE_VALUE) {
    if (error)
      error->code = ParseCode::FileOpenFailed;
    return false;
  }
  LARGE_INTEGER file_size;
  if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart < 0 ||
      static_cast<unsigned __int64>(file_size.QuadPart) >
          0x7FFFFFFFFFFFFFFFULL) {
    CloseHandle(file);
    if (error)
      error->code = ParseCode::FileReadFailed;
    return false;
  }
  size_type size = static_cast<size_type>(file_size.QuadPart);
  size_type capacity = size + size * 2 + 4096;
  Arena *arena = static_cast<Arena *>(
      HeapAlloc(GetProcessHeap(), 0, sizeof(Arena) + capacity));
  if (!arena) {
    CloseHandle(file);
    if (error)
      error->code = ParseCode::OutOfMemory;
    return false;
  }
  arena->memory = reinterpret_cast<unsigned char *>(arena + 1);
  arena->capacity = capacity;
  arena->used = size;
  unsigned char *target = arena->memory;
  size_type read_total = 0;
  while (read_total < size) {
    DWORD request = static_cast<DWORD>(
        (size - read_total) > 0x40000000ULL ? 0x40000000UL : size - read_total);
    DWORD got = 0;
    if (!ReadFile(file, target + read_total, request, &got, 0) || got == 0) {
      HeapFree(GetProcessHeap(), 0, arena);
      CloseHandle(file);
      if (error)
        error->code = ParseCode::FileReadFailed;
      return false;
    }
    read_total += got;
  }
  CloseHandle(file);
  document->root_node = 0;
  document->arena = arena;
  if (!parse_impl(document, reinterpret_cast<const char *>(target), size, error,
                  arena)) {
    destroy(document);
    return false;
  }
  return true;
}

void destroy(Document *document) {
  if (!document || !document->arena)
    return;

  HeapFree(GetProcessHeap(), 0, document->arena);
  document->arena = 0;
  document->root_node = 0;
}

const char *attribute(const Node *node, Slice name, size_type *value_size) {
  if (value_size)
    *value_size = 0;
  if (!node)
    return 0;

  for (Attribute *current = node->first_attribute; current;
       current = current->next) {
    if (same(current->name, name)) {
      if (value_size)
        *value_size = current->value.size;
      return current->value.data;
    }
  }

  return 0;
}

const Node *Node::element(Slice name) const { return child(this, name); }

const Node *Node::element(const char *name) const {
  return element({name, length(name)});
}

const char *Node::attribute(Slice name, size_type *value_size) const {
  return xml::attribute(this, name, value_size);
}

Slice Node::attribute(Slice name) const {
  size_type size = 0;
  const char *data = attribute(name, &size);
  return {data, size};
}

Slice Node::attribute(const char *name) const {
  return attribute({name, length(name)});
}

Slice Node::Attribute(const char *name) const { return attribute(name); }

const char *Node::attribute(const char *name, size_type *value_size) const {
  return attribute({name, length(name)}, value_size);
}

bool Document::parse(const char *source, size_type size, Error *error) {
  return xml::parse(this, source, size, error);
}

bool Document::parse(const char *source, Error *error) {
  size_type size = 0;

  while (source[size])
    ++size;

  return parse(source, size, error);
}

XElement::XElement() : root_(0), arena_(0) {}

XElement::XElement(XElement &&other) noexcept
    : root_(other.root_), arena_(other.arena_) {
  other.root_ = 0;
  other.arena_ = 0;
}

XElement &XElement::operator=(XElement &&other) noexcept {
  if (this != &other) {
    if (arena_) {
      Document document = {root_, arena_};
      destroy(&document);
    }
    root_ = other.root_;
    arena_ = other.arena_;
    other.root_ = 0;
    other.arena_ = 0;
  }
  return *this;
}

XElement::~XElement() {
  if (arena_) {
    Document document = {root_, arena_};
    destroy(&document);
  }
}

XElement XElement::parse(const char *source, Error *error) {
  XElement result;
  Document document = {};
  if (!document.parse(source, error))
    return result;
  result.root_ = document.root_node;
  result.arena_ = document.arena;
  document.root_node = 0;
  document.arena = 0;
  return result;
}

Node *XElement::element(const char *name) {
  return root_ ? const_cast<Node *>(root_->element(name)) : 0;
}

const Node *XElement::element(const char *name) const {
  return root_ ? root_->element(name) : 0;
}

Slice XElement::Attribute(const char *name) const {
  return root_ ? root_->Attribute(name) : Slice{0, 0};
}

const Node *child(const Node *node, Slice name) {
  if (!node)
    return 0;

  for (Node *current = node->first_child; current;
       current = current->next_sibling) {
    if (current->type == NodeType::Element && same(current->name, name))
      return current;
  }

  return 0;
}

const Node *next(const Node *node, Slice name) {
  if (!node)
    return 0;

  for (Node *current = node->next_sibling; current;
       current = current->next_sibling) {
    if (current->type == NodeType::Element && same(current->name, name))
      return current;
  }

  return 0;
}

} // namespace xml
