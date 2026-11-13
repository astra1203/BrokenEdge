"""Check audited native runtime asset paths against source files and cook roots.

Pure Python: no editor, build, registry mutation, or asset loading. This checks
DefaultGame.ini policy, not cooked dependencies or packaged runtime success.
Native constructor references are checked for source presence separately; they
need not each have an AlwaysCook root because the cooker follows hard references.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGING_SECTION = '/Script/UnrealEd.ProjectPackagingSettings'
# These sites load native string/soft defaults at runtime rather than depending
# on a particular map or blueprint to serialize the same reference first.
RUNTIME_SOURCES = (
    'Source/BrokenEdge/Extraction/ExtractionPlayerController.h',
    'Source/BrokenEdge/Extraction/ExtractionAbilities.h',
    'Source/BrokenEdge/Extraction/ExtractionKeyRoom.cpp',
    'Source/BrokenEdge/Extraction/WeaponRangeGameMode.cpp',
    'Source/BrokenEdge/Traversal/ExtractionTraversalComponent.cpp',
)
REGISTRY_EXPORT = 'Config/ExtractionItemRegistry.json'
GAME_LITERAL = re.compile(r'"(/Game/[^"\r\n]+)"')


def package_path(object_path):
    """Convert a Game object or generated-class path to its package name."""
    value = object_path.strip()
    if "'" in value:
        value = value.split("'", 1)[1].rstrip("'")
    value = value.split('.', 1)[0]
    if (not value.startswith('/Game/') or value.endswith('/')
            or '%' in value or any(part in ('', '.', '..') for part in value.split('/')[1:])):
        raise ValueError(f'Not a concrete Game package: {object_path}')
    return value


def cook_directories(ini):
    """Read the Packaging section's array additions, removals and clears."""
    section = ''
    result = []
    for line in ini.splitlines():
        line = line.strip()
        if not line or line.startswith((';', '#')):
            continue
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1]
            continue
        if section != PACKAGING_SECTION:
            continue
        match = re.match(r'([+.!-]?)DirectoriesToAlwaysCook\s*=\s*(.*)', line)
        if not match:
            continue
        operation, value = match.groups()
        if operation == '!':
            result.clear()
            continue
        path_match = re.search(r'Path\s*=\s*"([^"]+)"', value)
        if not path_match:
            raise ValueError(f'Unsupported cook directory: {line}')
        path = path_match[1].rstrip('/')
        if operation == '-':
            result = [entry for entry in result if entry != path]
        elif path not in result:
            result.append(path)
    return result


def covered_by(package, roots):
    return any(package.startswith(root.rstrip('/') + '/') for root in roots)


def registry_packages(root):
    """Read an editor export bound to the exact serialized registry asset."""
    root = Path(root)
    data = json.loads((root / REGISTRY_EXPORT).read_text(encoding='utf-8-sig'))
    if data.get('schema_version') != 1 or not data.get('entries'):
        raise ValueError('Missing/unsupported item registry export')
    package = package_path(data['registry'])
    asset = source_asset(root, package)
    if not asset or hashlib.sha256((root / asset).read_bytes()).hexdigest() != data.get('registry_sha256'):
        raise ValueError('Registry export is stale or its source is missing; run item-registry-export')
    identities = list(data['entries'])
    if any(not name or name.lower() == 'none' for name in identities) or len({name.lower() for name in identities}) != len(identities):
        raise ValueError('Empty or duplicate stable item IDs')
    return [package] + sorted({package_path(value) for value in data['entries'].values()})


def inspect_cooked_output(packages, cooked_content):
    """Check loose cook output under <platform>/<project>/Content, before packing."""
    if cooked_content is None:
        return dict(status='not_run', missing=[])
    content = Path(cooked_content)
    if not content.is_dir():
        raise ValueError(f'Cooked Content directory does not exist: {content}')
    missing = []
    for package in packages:
        relative = package.removeprefix('/Game/')
        if not any((content / (relative + suffix)).is_file() for suffix in ('.uasset', '.umap')):
            missing.append(package)
    return dict(status='passed' if not missing else 'failed', content=str(content.resolve()), missing=missing)


def source_asset(root, package):
    for suffix in ('.uasset', '.umap'):
        candidate = root / 'Content' / (package.removeprefix('/Game/') + suffix)
        if candidate.is_file():
            with candidate.open('rb') as stream:
                if stream.read(128).startswith(b'version https://git-lfs.github.com/spec/v1'):
                    return None
            return candidate.relative_to(root).as_posix()
    return None


def inspect_requirement(root, package, sources, cook_roots):
    asset = source_asset(root, package)
    return dict(package=package, sources=sorted(sources), source_asset=asset,
                cook_directory_covered=covered_by(package, cook_roots))


def audit(root, cooked_content=None):
    root = Path(root)
    roots = cook_directories((root / 'Config/DefaultGame.ini').read_text(encoding='utf-8-sig'))
    required = {}
    for source in RUNTIME_SOURCES:
        paths = GAME_LITERAL.findall((root / source).read_text(encoding='utf-8-sig'))
        if not paths:
            raise ValueError(f'No runtime asset literals in audited site: {source}')
        for path in paths:
            required.setdefault(package_path(path), set()).add(source)
    # Map definitions moved from native literals into the authored config registry.
    config = (root / 'Config/DefaultGame.ini').read_text(encoding='utf-8-sig')
    for path in re.findall(r'^\+?(?:Maps|MapDefinition)\s*=\s*(/Game/[^\s]+)', config, re.M):
        required.setdefault(package_path(path), set()).add('Config/DefaultGame.ini:MapRegistry')
    for package in registry_packages(root):
        required.setdefault(package, set()).add(REGISTRY_EXPORT)
    requirements = [inspect_requirement(root, package, sources, roots)
                    for package, sources in sorted(required.items())]
    issues = []
    for entry in requirements:
        if not entry['source_asset']:
            issues.append('missing_source:' + entry['package'])
        if not entry['cook_directory_covered']:
            issues.append('missing_cook_root:' + entry['package'])
    # Inventory the remaining literal references too, including hard constructor
    # references and travel maps. Formatted paths are handled by the ID audit.
    literals = set()
    for source in (root / 'Source/BrokenEdge').rglob('*'):
        if source.suffix not in ('.cpp', '.h') or 'Tests' in source.parts:
            continue
        for path in GAME_LITERAL.findall(source.read_text(encoding='utf-8-sig')):
            if '%' not in path and not path.endswith('/'):
                literals.add(package_path(path))
    for package in sorted(literals - required.keys()):
        if not source_asset(root, package):
            issues.append('missing_literal_source:' + package)
    cooked = inspect_cooked_output(required.keys(), cooked_content)
    issues.extend('missing_cook_output:' + package for package in cooked['missing'])
    stages = dict(source='failed' if any('source:' in x for x in issues) else 'passed',
                  cook_policy='failed' if any(x.startswith('missing_cook_root:') for x in issues) else 'passed',
                  cook_output=cooked, packaged_runtime='not_run')
    return dict(passed=not issues, all_four_stages_passed=False, stages=stages,
                scope='Source/cook policy; optional loose cook output. Packaged Client/Server gameplay requires a separate run.',
                cook_directories=roots, runtime_requirements=requirements,
                other_literal_packages=sorted(literals - required.keys()), issues=issues)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, default=ROOT)
    parser.add_argument('--cooked-content', type=Path, help='Loose cooked <platform>/<project>/Content directory')
    parser.add_argument('--json', action='store_true', help='Print the full audited package inventory')
    args = parser.parse_args()
    try:
        result = audit(args.project, args.cooked_content)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Cook coverage audit failed: {error}\n')
    if args.json:
        print(json.dumps(result, ensure_ascii=True, indent=2))
    else:
        print(f"{'PASS' if result['passed'] else 'FAIL'}: {len(result['runtime_requirements'])} runtime packages, "
              f"{len(result['other_literal_packages'])} other literal packages; {result['scope']}")
        for issue in result['issues']:
            print(issue)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
