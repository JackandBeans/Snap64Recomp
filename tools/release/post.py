"""A release announcement on GitHub Discussions and the pinned "What's next" (discussion 1), the house way.
Without --post it only prepares and shows: the ids, the live pinned body (saved in a temporary folder as
pinned_before.md), the two new bodies and their word counts, and the words that must stay out. With
--post it edits the pinned discussion, creates the announcement in Announcements, and reads both back.

    tools/release/post.py "<title>" <announcement file> <pinned file> [--post]

--post needs the go marker .claude/GO (made on the author's word, less than three hours old).
Environment: SNAP64_REPO (default from origin)."""
import io
import json
import os
import subprocess
import sys
import time

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
ROOT = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip()
REPO = os.environ.get('SNAP64_REPO') or subprocess.run(['git', 'remote', 'get-url', 'origin'], capture_output=True, text=True).stdout.strip().split('github.com')[-1].lstrip(':/').removesuffix('.git')
OWNER, NAME = REPO.split('/')
import tempfile

S = tempfile.mkdtemp(prefix='snap64-post-').replace('\\', '/')     # the bodies as sent, and the pinned body before
if len(sys.argv) < 4:
    sys.exit(__doc__)
print('working files in', S)
TITLE = sys.argv[1]
post = io.open(sys.argv[2], encoding='utf-8').read().replace('\r\n', '\n').strip() + '\n'
pinned = io.open(sys.argv[3], encoding='utf-8').read().replace('\r\n', '\n').strip() + '\n'
for name, text in (('post_body.md', post), ('pinned_body.md', pinned)):
    io.open(S + '/' + name, 'w', encoding='utf-8', newline='\n').write(text)


def gql(query, **vars):
    cmd = ['gh', 'api', 'graphql', '-f', 'query=' + query]
    for k, v in vars.items():
        cmd += (['-F', '%s=@%s' % (k, v[1:])] if v.startswith('@') else ['-f', '%s=%s' % (k, v)])
    r = subprocess.run(cmd, capture_output=True)
    out = r.stdout.decode('utf-8', 'replace')
    if r.returncode != 0:
        sys.exit('FAILED: ' + out + r.stderr.decode('utf-8', 'replace'))
    return json.loads(out)['data']


info = gql('''query { repository(owner: "%s", name: "%s") { id
  discussionCategories(first: 20) { nodes { id name } }
  discussion(number: 1) { id title body url category { name } }
  discussions(first: 5, orderBy: {field: CREATED_AT, direction: DESC}) { nodes { number title } }
  pinnedDiscussions(first: 5) { nodes { discussion { number title } } } } }''' % (OWNER, NAME))['repository']
cat = next(n['id'] for n in info['discussionCategories']['nodes'] if n['name'] == 'Announcements')
one = info['discussion']
io.open(S + '/pinned_before.md', 'w', encoding='utf-8', newline='\n').write(one['body'].replace('\r\n', '\n'))
print('pinned now:', [(p['discussion']['number'], p['discussion']['title']) for p in info['pinnedDiscussions']['nodes']])
print('newest:', [(n['number'], n['title']) for n in info['discussions']['nodes']])
if any(n['title'] == TITLE for n in info['discussions']['nodes']):
    sys.exit('"%s" already exists; nothing posted' % TITLE)
print('#1 is "%s" in %s, %d words now; first line: %s' % (one['title'], one['category']['name'], len(one['body'].split()), one['body'].split('\n')[0][:90]))
print('new pinned body: %d words; first line: %s' % (len(pinned.split()), pinned.split('\n')[0][:90]))
print('new post "%s": %d words; first line: %s' % (TITLE, len(post.split()), post.split('\n')[0]))
print('  last line: %s' % post.strip().split('\n')[-1][:110])
bad = False
# The words of unannounced work are not in the tree: they are in
# .claude/private-words.txt on the release machine, one a line, and without
# that file nothing is posted.
private = os.path.join(ROOT, '.claude', 'private-words.txt')
try:
    secret_words = [w.strip() for w in io.open(private, encoding='utf-8') if w.strip() and not w.startswith('#')]
except OSError:
    secret_words = []
    print('  !! %s is missing: the words that must stay out cannot be checked' % private)
    bad = True
for word in ('Claude', 'Anthropic', ' AI ', '<link', 'LINK-TO', '{CI_', '{LOCAL_') + tuple(secret_words):
    for name, text in (('post', post), ('pinned', pinned)):
        if word in text:
            print('  !! "%s" appears in the %s body' % (word.strip(), name))
            bad = True
if not (121 <= len(post.split()) <= 258):
    print('  !! the post has %d words; the announcements run 121 to 258' % len(post.split()))
if bad or '--post' not in sys.argv:
    print('dry run only')
    sys.exit(0)
marker = os.path.join(ROOT, '.claude', 'GO')
try:
    assert time.time() - os.path.getmtime(marker) < 3 * 3600
except (OSError, AssertionError):
    sys.exit('no go marker: nothing is posted without the author\'s go (touch .claude/GO on his word)')
r = gql('mutation($id: ID!, $body: String!) { updateDiscussion(input: {discussionId: $id, body: $body}) { discussion { url updatedAt } } }',
        id=one['id'], body='@' + S + '/pinned_body.md')
print('edited:', r['updateDiscussion']['discussion'])
r = gql('mutation($repo: ID!, $cat: ID!, $title: String!, $body: String!) { createDiscussion(input: {repositoryId: $repo, categoryId: $cat, title: $title, body: $body}) { discussion { number url } } }',
        repo=info['id'], cat=cat, title=TITLE, body='@' + S + '/post_body.md')
new = r['createDiscussion']['discussion']
print('created:', new)
back = gql('''query { repository(owner: "%s", name: "%s") {
  a: discussion(number: 1) { body } b: discussion(number: %d) { title body category { name } author { login } } } }''' % (OWNER, NAME, new['number']))['repository']
print('pinned reads back the same:', back['a']['body'].replace('\r\n', '\n').strip() == pinned.strip())
print('post reads back the same:', back['b']['body'].replace('\r\n', '\n').strip() == post.strip(), '|', back['b']['title'], '|', back['b']['category']['name'], '|', back['b']['author']['login'])
