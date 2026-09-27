#pragma once

namespace xml {
using size_type = unsigned __int64;

struct Slice {
  const char *data;
  size_type size;
};

enum class NodeType : unsigned char {
  Element,
  Text,
  CData,
  Comment,
  ProcessingInstruction
};

struct Attribute {
  Slice name;
  Slice value;
  Attribute *next;
};

struct Node {
  NodeType type;
  Slice name;
  Slice value;
  Attribute *first_attribute;
  Node *first_child;
  Node *last_child;
  Node *next_sibling;

  const Node *element(Slice name) const;
  const Node *element(const char *name) const;
  Slice attribute(Slice name) const;
  Slice attribute(const char *name) const;
  const char *attribute(Slice name, size_type *value_size) const;
  const char *attribute(const char *name, size_type *value_size) const;
  Slice Attribute(const char *name) const;
};

enum class ParseCode : unsigned long {
  Ok,
  EmptyDocument,
  InvalidName,
  InvalidSyntax,
  InvalidEntity,
  UnknownEntity,
  DuplicateAttribute,
  MismatchedTag,
  UnexpectedEnd,
  InvalidComment,
  InvalidCData,
  InvalidProcessingInstruction,
  FileOpenFailed,
  FileReadFailed,
  OutOfMemory
};

struct Error {
  ParseCode code;
  size_type offset;
  size_type line;
  size_type column;
};

struct Document {
  Node *root_node;
  void *arena;

  Node *root() { return root_node; }
  const Node *root() const { return root_node; }

  Node *element(const char *name) {
    return root_node ? const_cast<Node *>(root_node->element(name)) : 0;
  }

  const Node *element(const char *name) const {
    return root_node ? root_node->element(name) : 0;
  }

  bool parse(const char *source, size_type size, Error *error);
  bool parse(const char *source, Error *error);

  template <size_type size>
  bool parse(const char (&source)[size], Error *error) {
    return parse(source, size - 1, error);
  }
};

class XElement {
public:
  XElement();
  XElement(const XElement &) = delete;
  XElement &operator=(const XElement &) = delete;
  XElement(XElement &&other) noexcept;
  XElement &operator=(XElement &&other) noexcept;
  ~XElement();

  static XElement parse(const char *source, Error *error = 0);

  template <size_type size>
  static XElement parse(const char (&source)[size], Error *error = 0) {
    return parse(source, error);
  }

  Node *element(const char *name);
  const Node *element(const char *name) const;
  Slice Attribute(const char *name) const;
  bool valid() const { return root_ != 0; }

private:
  Node *root_;
  void *arena_;
};

bool parse(Document *document, const char *source, size_type size,
           Error *error);
bool parse_file(Document *document, const char *path, Error *error);
void destroy(Document *document);

bool equal(Slice left, const char *right);
const char *attribute(const Node *node, Slice name, size_type *value_size);
const Node *child(const Node *node, Slice name);
const Node *next(const Node *node, Slice name);
} // namespace xml
