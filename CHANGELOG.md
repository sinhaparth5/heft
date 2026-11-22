# Changelog

## [Unreleased]

## [2026.11.5] - 2026-11-22
### Added
- Docker tab (5): containers, images, volumes and networks with their sizes, read from the Docker API socket. `u` removes the selected item and `t` starts or stops a container, each after a confirmation. Removing a running container or a built-in network (`bridge`, `host`, `none`) is refused; images are removed with all their tags.
