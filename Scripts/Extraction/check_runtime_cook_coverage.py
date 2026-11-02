"""Check audited native runtime asset paths against source files and cook roots.

Pure Python: no editor, build, registry mutation, or asset loading. This checks
DefaultGame.ini policy, not cooked dependencies or packaged runtime success.
Native constructor references are checked for source presence separately; they
need not each have an AlwaysCook root because the cooker follows hard references.
"""
import argparse
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
    'Source/BrokenEdge/Extraction/ExtractionTacticalMap.cpp',
    'Source/BrokenEdge/Extraction/WeaponRangeGameMode.cpp',
    'Source/BrokenEdge/Traversal/ExtractionTraversalComponent.cpp',
)
ITEM_SOURCE = 'Source/BrokenEdge/Extraction/ExtractionItems.cpp'
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


def item_packages(source):
    match = re.search(r'UExtractionItemDefinition::Ids\(\).*?Names\s*=\s*\{(.*?)\};', source, re.S)
    if not match or '"/Game/Extraction/Items/DA_Item_%s.DA_Item_%s"' not in source:
        raise ValueError('Item loader or ID declaration changed; update the cook audit')
    names = re.findall(r'TEXT\("([A-Za-z0-9_]+)"\)', match[1])
    if not names or len(set(names)) != len(names):
        raise ValueError('Item ID list is empty or duplicated')
    return [f'/Game/Extraction/Items/DA_Item_{name}' for name in names]


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


def audit(root):
    root = Path(root)
    roots = cook_directories((root / 'Config/DefaultGame.ini').read_text(encoding='utf-8-sig'))
    required = {}
    for source in RUNTIME_SOURCES:
        paths = GAME_LITERAL.findall((root / source).read_text(encoding='utf-8-sig'))
        if not paths:
            raise ValueError(f'No runtime asset literals in audited site: {source}')
        for path in paths:
            required.setdefault(package_path(path), set()).add(source)
    for package in item_packages((root / ITEM_SOURCE).read_text(encoding='utf-8-sig')):
        required.setdefault(package, set()).add(ITEM_SOURCE + ':UExtractionItemDefinition::Get/Ids')
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
    return dict(passed=not issues, scope='source existence and DefaultGame.ini cook policy only',
                cook_directories=roots, runtime_requirements=requirements,
                other_literal_packages=sorted(literals - required.keys()), issues=issues)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, default=ROOT)
    parser.add_argument('--json', action='store_true', help='Print the full audited package inventory')
    args = parser.parse_args()
    try:
        result = audit(args.project)
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
