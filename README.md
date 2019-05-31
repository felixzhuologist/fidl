# FIDL compiler

## Pipeline

### Overview

The main inputs of interest to the compiler are the arguments to the `--files`
flags, which describe a list of files grouped by library. We parse each file
individually to obtain a parse tree for each file:

1. Each file is loaded into a [`SourceFile`](#sourcefile), which owns the buffer backing the file
and provides low level methods into the file's data
2. We initialize a [`Lexer`](#lexing), which takes the `SourceFile` as an argument to its constructor.
This class exposes a `Lex()` method, which generates a sequence of [`Token`](#token)s.
3. We initialize a [`Parser`](#parsing) using the `Lexer`, and call the `Parse()` method which constructs a parse tree, referred to as a "raw" AST. The function returns a `raw::File` which is the class representing the root node of the raw AST.

Now that we have parsed each file into a parser tree, we then work to group the 
parser trees into a single flat AST representation for each library. The root of this AST
for a single library is a `flat::Library`.

1. For each library, we traverse the `raw::File`s containing its definitions, converting
nodes to their "flat" counterparts (e.g. a `raw::StructDeclaration` becomes a
`flat::Struct`, `raw::CompoundIdentifier` becomes a [`flat::Name`](#name)), and
storing them into a single `flat::Library`.
2. Once the AST has been fully initialized, the compiler evaluates constants and
determines the memory alignment information for the declared types

Finally, we end up with a flat AST that is processed and ready for backend
generation - either to C bindings or to a JSON IR.

### <a name="lexing"></a> Lexing
The `Lexer` works primarily by keeping track of two `char *` pointer into the file data - one which marks the current location that the class is at (`current_`), and one which marks the start of the lexeme that is currently being worked on (`token_start_`). Each time the `Lex()` method is called, the `current_` pointer is advanced until a complete lexeme has been traversed, and a `Token` is constructed using the data in between the two pointers.

The lexer also keeps track of a third `previous_end_` pointer so it can get the
data between lexemes (generally whitespace) when it constructs the `Token`. To
illustrate how this works, here is an example of how the pointers change during
a call to `Lex()` on the short fidl snippet `const bool flag = true;`:

initial state after lexing the `const` keyword:
```
 const bool flag = true;
      ^current_
      ^token_start_
      ^previous_end_
```

we skip any whitespace until the next lexeme
```
const bool flag = true;
      ^current_
      ^token_start_
     ^previous_end_ 
```

then update `current_` until the end of the lexeme
```
const bool flag = true;
          ^current_
      ^token_start_
     ^previous_end_ 
```

at this point, we are ready to construct the next `Token` that gets returned. We
create a `Token` with its `previous_end_` argument set to the data between
`previous_end_` and `token_start_`, its `location_` set to the data between
`token_start_` and `current_`, and its kind to `Identifier`. Before returning,
we reset the pointers and end up in a state similar to the initial state,
where we can repeat the process for the next token:
```
 const bool flag = true;
           ^current_
           ^token_start_
           ^previous_end_
```

Internally the two pointers are manipulated via three main methods:

1. `Skip()`, which simply skips over any characters that we don't care about (e.g. whitespace) by moving both pointers forward.
2. `Consume()`, which returns the current char and advances `current_`. 
3. `Reset()`, returns the data between `token_start_` and `current_`, then sets `token_start_` to the value of `current_`.

### <a name="parsing"></a> Parsing
The Parser's goal is to convert a `Token` stream from a single FIDL file (generated using the Lexer) into a parse tree (referred to as a "raw" AST) via the `Parse()` method, and is implemented using [recursive descent](https://en.wikipedia.org/wiki/Recursive_descent_parser). Each node of the raw AST (which is just a [start/end token pair](#sourceelement) along with pointers to any children) has a corresponding `ParseFoo()` method that consumes `Token`s from the `Lexer` and returns a `unique_ptr` to an instance of that node, or a nullptr on failure.

The `Parser` keeps track of the current nodes that are being build using a stack of [`SourceElements`](#sourceelement) (`active_ast_scopes_`) as well as the current and previous `Token`s that are being processed (`last_token_` and `previous_token_`, respectively). 

The parser determines what kind of node the current `Token` belongs to based
on the `Token::Kind` of `last_token_` (via the `Peek()` method), and updates its
state and constructs the nodes through the use of the `ASTScope` class as well
as the `ConsumeToken`/`MaybeConsumeToken` helper methods.
In order to explain how they work, let's walk through an example of a simple nonrecursive case line by line.
The parser method looks like this:
```c++
std::unique_ptr<raw::StringLiteral> Parser::ParseStringLiteral() {
    ASTScope scope(this);
    ConsumeToken(OfKind(Token::Kind::kStringLiteral));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::StringLiteral>(scope.GetSourceElement());
}
```
it consumes a single token and returns a leaf node, `raw::StringLiteral`:
```c++
class StringLiteral : public SourceElement {
public:
    explicit StringLiteral(SourceElement const& element) : SourceElement(element) {}

    virtual ~StringLiteral() {}
}
```
The method starts by creating a new `ASTScope`, which initializes the `SourceElement`
that will later be used to create the returned node, by pushing it onto `active_ast_scopes_`;
the start of the `SourceElement` will get set to the first token we consume, and the end gets set
when we construct the new node later. Here is the line in question inside the `ASTScope` constructor:
```c++
parser_->active_ast_scopes_.push_back(raw::SourceElement(Token(), Token()));
```

We then call `ConsumeToken` to process the next token. This method takes a predicate
(which we construct using `OfKind()`, which returns a function that checks that its input
matches the given kind/subkind) and calls that predicate on `last_token_`'s kind/subkind.
If the predicate fails (in this case, if the current token is not a string literal),
the error gets stored on the class and the method returns, and the error
then gets caught by the `Ok()` call on the following line which stops the compiler
with a parsing error. If the predicate succeeds,
we set the start token of any `SourceElement`s in the stack that are uninitialized, to the current token:
```c++
for (auto& scope : active_ast_scopes_) {
    if (scope.start_.kind() == Token::Kind::kNotAToken) {
        scope.start_ = token;
    }
}
```
In the context of this example, this will set the start token of the top element
of the stack since it was just initialized at the start of the method. The parser
then advances to the next token by setting `previous_token_ = last_token_` and `last_token_ = Lex()`.

Finally, we return the resulting `StringLiteral` node using `scope.GetSourceElement()`:
this will set the end token of the `SourceElement` at the top of the stack to the `previous_token_`, and then return the `SourceElement`. We end up with a node that has the same start/end token, since
`StringLiteral`s are only ever a single token long, but other methods may
consume many tokens before calling `GetSourceElement()`. When the method returns, the destructor of
`ASTScope` pops the top element off of `active_ast_scopes_`.

There are some other steps that happen under the hood (for example, we change the `previous_end_` of the end token of each `SourceElement` to reflect the gap between the start and end tokens), but the above example illustrates the general structure that all of the parser methods have.

### Flattening

After we have parsed each file into a `raw::File`, and have initialized an empty
AST (`flat::Library`) for each library, we need to
update the AST with all of data from the `raw::File`s that correspond to it. This is done
recursively using the `ConsumeFoo()` methods, which each generally take the corresponding
raw AST node as input, update the `Library` class, then return a bool to indicate
success/failure. These methods are responsible for:

  - Checking for any undefined library dependencies (e.g. using `textures.Foo` will error if the `textures` library was not imported)
  - Converting the raw AST nodes into their flat AST node equivalents, and storing them in the `Library`'s `foo_declarations_` attribute. Initially the values of the flat AST nodes are unset but they get calculated later during compilation.
  - Registering each [declaration](#decl) by adding them to the `declarations_` vector. Const declarations (which declare a value) are also added to the `constants_` vector, whereas all other declarations (which declare a type) get their corresponding [type template](#typetemplate) added to the library's [typespace](#typespace).

### Compilation

Once all of the `Decl`s for a given `Library` have been added to the `declarations_`
vector, the compiler can proceed to compile each individual declaration. However,
it must do this in the correct order (so that any dependencies of a declaration
are compiled before it) and in the FIDL compiler this is done by first sorting
the declarations into a separate `declarations_order_` vector, and then iterating
through it to compile each declaration. The sorting is done in the `SortDeclarations()`
method which makes use of `DeclDependencies()` to get the dependencies for a
given declaration.

Given the sorted declarations, the compilation happens via the `CompileFoo` methods,
generally correponding to the AST nodes (e.g. `CompileStruct`, `CompileConst`), with
`CompileDecl` as the entrypoint. The main purpose of `CompileDecl` is:

  - for [`TypeDecl`s](#decl), to determine their [`TypeShape`](#typeshape)
  - for [`Const`s](#decl), to resolve their value, and
  - for`TypeConstructor`s, to set their [`Type`](#type).

The compilation process does other things like validate the library name and
do attribute checking.

TODO: go through some compilation examples of the above

Once this step is complete, the `flat::Library` contains all the necessary information
for any code generation. The FIDL compiler can directly generate C bindings, or can
generate a JSON IR that can be consumed by a separate backend

### Glossary

#### <a name="sourcefile"></a> SourceFile
Wrapper around a file which is responsible for owning the data in that file.

#### <a name="sourcemanager"></a> SourceManager
Wrapper around a set of `SourceFile`s that all relate to a single `Library`.

#### <a name="sourcelocation"></a> SourceLocation
Wrapper around a `StringView` and the `SourceFile` it comes from. It provides methods to get the surrounding line of the `StringView` as well as its location in the form of a `"[filename]:[line]:[col]"` string

#### <a name="token"></a> Token
A token is essentially a lexeme (in the form of a `SourceLocation`stored as the
`location_` attribute), enhanced with two other pieces information that are useful
to the parser during the later stages of compilation:

1. `previous_end_`, a `SourceLocation` which starts at the end of the previous token and ends at the start of this one. It contains data that is uninteresting to the parser i.e. whitespace.
2. A "kind" and "subkind" which together classifies the lexeme. The possible kinds are: the special characters (e.g. `Kind::LeftParen`, `Kind::Dot`, `Kind::Comma`, etc.), string/numeric constants, or identifiers. Tokens that are keywords (e.g. `const`, `struct`) are considered identifiers, but also have a subkind defined to identify which keyword it is (e.g. `Subkind::Struct`, `Subkind::Using`); all other tokens have a subkind of None. Unitialized tokens have a kind of `kNotAToken`.

#### <a name="sourceelement"></a> SourceElement
A SourceElement represents a block of code inside a fidl file and is parameterized by a
`start_` and `end_` `Token`. All parser tree ("raw" AST) nodes inherit from this class.

#### <a name="name"></a> Name
A `Name` represents a scope variable name, and consists of the library the
name belongs to (or `nullptr` for global names), and the variable name itself as
a `SourceLocation`.

#### <a name="decl"></a> Decl/TypeDecl
The `Decl` is the base of all flat AST nodes, just like `SourceElement` is the base of
all parser tree nodes, and corresponds to all possible declarations that a user can
make in a FIDL file. There are two types of `Decl`s: `Const`s, which declare a value,
and have a `value_` attribute that gets resolved during compilation, and `TypeDecl`s,
which declare a message type or interface and have a `typeshape_` attribute that
gets set during compilation. `TypeDecl`s also have a static `Shape()` method which
is what actually determines the typeshape of that specific type.

#### <a name="type"></a> Type
A struct representing an instance of a type. For example, the `vector<int32>:10?`
type corresponds to an instance of the `VectorType` with `max_size_ = 10` and 
`maybe_arg_type` set to the `Type` corresponding to `int32`. Built in types
all have a static `Shape()` method which will return the `Typeshape` given the
parameters for an instance of that type. User defined types (e.g. structs
or unions) will all have a type of `IdentifierType` - the corresponding [`TypeDecl`](#typedecl),
like `Struct` provides the static `Shape()` method instead.

TODO: discuss where/how types are used

#### <a name="typespace"></a> Typespace
The typespace is essentially a map from [`Type`](#type) names to a `TypeTemplate` for that
`Type`. When the compiler needs an instance of a specific type, it calls the
typespace's `Create()` method which looks up the desired type template and calls
its `Create()` method, passing in the given type parameters.
The typespace used during compilation is initialized to include all of the
built in types (e.g. `"vector"` maps to `VectorTypeTemplate`), and user defined
types get added during the compilation process.

#### <a name="typetemplate"></a> TypeTemplate
Instances of TypeTemplates provide a `Create()` method to create a new instance
of a specific `Type` - therefore there is a TypeTemplate subclass for each
built in FIDL type (e.g. `ArrayTypeTemplate`, `PrimitiveTypeTemplate`, etc.) as
well as a single class for all user defined types (`TypeDeclTypeTemplate`),
and one for type aliases (`TypeAliasTypeTemplate`). `Create()`
takes as parameters the possible type parameters: argument type, nullability,
and size. For example, to create an object representing the type `vector<int32>:10?`
we would call the `Create()` method of the `VectorTypeTemplate`, with an
argument type of `int32`, max size of `10`, and a nullability of `types::Nullability::kNullable`, which
would return an instance of a `VectorType` with those parameters. Note that not all 3 of these
parameters apply to all of the types (e.g. `PrimitiveType`s, like `int32` have none of the 3) -
the `Create()` method of the type template for each type automatically checks
that only the relevant parameters are passed in.

The concrete type for user
defined types is the `IdentifierType`, which gets generated by the `TypeDeclTypeTemplate`.

#### <a name="typeshape"></a> TypeShape
Information about how objects of a type should be laid out in memory, including
their size, alignment, depth, etc.

#### <a name="fieldshape"></a> FieldShape
The typeshape of a field, consisting of a `Typeshape` and an offset from the
start of the object.
