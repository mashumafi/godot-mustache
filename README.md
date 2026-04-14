# godot-mustache
A parser for the mustache spec tightly integrated with Godot through GDExtension.

## Spec

This implementation is compliant to all required features of the spec.

### Features

* Comments
* Delimiters
* Interpolation
* Inverted Sections
* Partials
* Sections

### Planned

* Dynamic Names
* Inheritance
* Lambdas

## Performance

The code is tuned for performance though the following principals:

* Data structures
  * Cache locality through flat collections (no nesting)
  * Allocate once by estimating and reserving expected size needed for containers
  * No string copying
* During execution
  * No virtual dispatching
  * No recursion

## Build

```shell
scons
```

### Debugging

```shell
scons dev_build=yes
```
