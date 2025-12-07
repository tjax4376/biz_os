# Contributing to BIZ_OS

## Development Setup

See [QUICK_START.md](QUICK_START.md) for detailed setup instructions.

## Code Standards

### Rust
- Follow Rust style guide
- Use `rustfmt` for formatting: `cargo fmt`
- Use `clippy` for linting: `cargo clippy`
- Run tests: `cargo test`

### Python
- Follow PEP 8
- Use `black` for formatting: `black .`
- Use `pylint` for linting: `pylint .`
- Run tests: `pytest`

### TypeScript/React
- Follow React best practices
- Use ESLint: `npm run lint`
- Use Prettier: `npm run format`
- Run tests: `npm test`

### C (Kernel)
- Follow Linux kernel coding style
- Use `checkpatch.pl` for style checking
- Document all functions

## Testing

- Write unit tests for all modules
- Write integration tests for layer interactions
- Write end-to-end tests for user workflows
- Run all tests before committing

## Git Workflow

- Create feature branches for each phase
- Use descriptive commit messages
- Tag releases at phase completions
- Keep commits atomic and focused

## AI-Assisted Development

- Use AI for boilerplate code generation
- Review all AI-generated code
- Test thoroughly
- Understand what the code does

## Documentation

- Document all public APIs
- Update README.md for major changes
- Keep architecture docs up to date
- Document design decisions in code comments

