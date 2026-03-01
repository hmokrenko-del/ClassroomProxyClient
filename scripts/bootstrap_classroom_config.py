from pathlib import Path
import shutil

Import("env")  # type: ignore # noqa: F821

project_dir = Path(env.subst("$PROJECT_DIR"))
pioenv = env.subst("$PIOENV")


def find_template(filename: str) -> Path | None:
    candidates = [
        project_dir / "include" / filename,
        project_dir / filename,
        project_dir / "lib" / "ClassroomProxyClient" / "templates" / filename,
        project_dir / ".pio" / "libdeps" / pioenv / "ClassroomProxyClient" / "templates" / filename,
    ]

    libdeps_dir = project_dir / ".pio" / "libdeps" / pioenv
    if libdeps_dir.exists():
        # Fallback for non-standard package folders (scoped names / hashed dirs).
        candidates.extend(libdeps_dir.glob(f"*/templates/{filename}"))

    return next((path for path in candidates if path.exists()), None)


def create_if_missing(target: Path, template_name: str, hint: str) -> None:
    if target.exists():
        print(f"[ClassroomProxyClient bootstrap] Keep existing file: {target}")
        return

    source = find_template(template_name)
    if source is None:
        print(f"[ClassroomProxyClient bootstrap] Template '{template_name}' not found. {hint}")
        return

    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    print(f"[ClassroomProxyClient bootstrap] Created {target} from {source}")


create_if_missing(
    project_dir / "include" / "classroom_config.h",
    "classroom_config.example.h",
    "Add include/classroom_config.example.h manually.",
)

create_if_missing(
    project_dir / ".env.example",
    ".env.example",
    "Add .env.example manually if you don't use proxy scripts.",
)

create_if_missing(
    project_dir / ".env",
    ".env.example",
    "Create .env manually and set GEMINI_API_KEY.",
)
