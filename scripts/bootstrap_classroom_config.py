from pathlib import Path
import shutil

Import("env")  # type: ignore # noqa: F821

project_dir = Path(env.subst("$PROJECT_DIR"))
lib_dir = Path(__file__).resolve().parents[1]
templates_dir = lib_dir / "templates"


def find_template(filename: str) -> Path | None:
    candidates = [
        project_dir / "include" / filename,
        project_dir / filename,
        templates_dir / filename,
    ]
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
