# Contributing

Thanks for your interest in contributing to Moonmic.

Before starting a larger change, check the open issues and pull requests to avoid duplicating work. If the change affects behavior, compatibility, or a supported platform in a significant way, consider opening an issue first to discuss the approach.

## Making changes

- Follow the existing code style and project structure.
- Keep changes focused on the problem being addressed.
- Add or update tests when behavior changes or when a regression can be reproduced automatically.
- Update the documentation when setup, configuration, public behavior, or platform support changes.
- Do not commit build output, temporary files, credentials, or local development artifacts.

## Testing

Run the checks that are relevant to the code you changed. Protocol changes can be tested with:

```sh
cmake -S . -B build/tests -DBUILD_CLIENT=OFF -DMOONMIC_BUILD_TESTS=ON
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

For host or platform-specific changes, also build the affected target and run the relevant host tests when the required platform and dependencies are available.

## Pull requests

Describe the problem being solved, the approach taken, and how the change was tested. Link any related issues and mention compatibility or platform-specific considerations when they matter.

Keep pull requests reasonably focused so they are easy to review and test.

## Commit messages

Use a short, descriptive subject. Add a body when the reason for a change or an important implementation detail is not obvious from the diff.
