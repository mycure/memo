import couchdb
import couchdb.client
import json
import os
import os.path
import requests
import shutil
import subprocess
import tempfile
import time

import infinit.beyond

class CouchDB:

  def __init__(self, port = 0, directory = None):
    self.beyond = None
    self.__delete = False
    self.__dir = directory
    self.__uri = None
    self.__port = port

  def __path(self, p):
    return '%s/couchdb.%s' % (self.__dir, p)

  def __enter__(self):
    if self.__dir is None:
      self.__dir = tempfile.mkdtemp()
      self.__delete = True
    else:
      try:
        os.makedirs(self.__dir)
      except FileExistsError:
        pass
    config = self.__path('ini')
    pid = self.__path('pid')
    stdout = self.__path('stdout')
    stderr = self.__path('stderr')
    with open(config, 'w') as f:
      print('''\
[couchdb]
database_dir = %(root)s/db-data
view_index_dir = %(root)s/db-data
uri_file = %(root)s/couchdb.uri

[httpd]
port = %(port)s

[log]
file = %(root)s/db.log

[query_servers]
python=python -m couchdb
''' % {'root': self.__dir, 'port': self.__port},
            file = f)
    try:
      os.remove(self.__path('uri'))
    except:
      pass
    subprocess.check_call(
      ['couchdb', '-a', config,
       '-b', '-p', pid, '-o', stdout, '-e', stderr])
    status_cmd = ['couchdb', '-a', config, '-p', pid, '-s']
    while subprocess.call(status_cmd, stderr = subprocess.PIPE) != 0:
      time.sleep(0.1)
    while not os.path.exists(self.__path('uri')):
      time.sleep(0.1)
    with open(self.__path('uri'), 'r') as f:
      self.__uri = f.read().strip()
    while True:
      try:
        if requests.get(self.__uri).json()['couchdb'] == 'Welcome':
          break
      except requests.exceptions.ConnectionError:
        time.sleep(0.1)
    return couchdb.Server(self.__uri)

  def __exit__(self, *args, **kwargs):
    subprocess.check_call(['couchdb', '-d', '-p', self.__path('pid')])
    if self.__delete:
      shutil.rmtree(self.__dir)

  @property
  def uri(self):
    return self.__uri

def getsource(f):
  import inspect
  lines = inspect.getsource(f).split('\n')
  pad = 0
  while pad < len(lines[0]) and lines[0][pad] == ' ':
    pad += 1
  return '\n'.join(line[min(len(line), pad):] for line in lines)

class CouchDBDatastore:

  def __init__(self, db):
    self.__couchdb = db
    def create(name):
      try:
        self.__couchdb.create(name)
      except couchdb.http.PreconditionFailed as e:
        if e.args[0][0] == 'file_exists':
          pass
        else:
          raise
    create('networks')
    create('users')
    create('volumes')
    import inspect
    try:
      design = self.__couchdb['users']['_design/beyond']
    except couchdb.http.ResourceNotFound:
      design = couchdb.client.Document()
    design.update(
      {
        '_id': '_design/beyond',
        'language': 'python',
        'updates': {
          name: getsource(update)
          for name, update in [
              ('update', self.__user_update),
          ]
        },
        'views': {
          name: {
            'map': getsource(view),
          }
          for name, view in [('per_name', self.__user_per_name)]
        }
      })
    self.__couchdb['users'].save(design)
    try:
      design = self.__couchdb['networks']['_design/beyond']
    except couchdb.http.ResourceNotFound:
      design = couchdb.client.Document()
    design.update(
      {
        '_id': '_design/beyond',
        'language': 'python',
        'updates': {
          name: getsource(update)
          for name, update in [
              ('update', self.__network_update),
          ]
        },
      })
    self.__couchdb['networks'].save(design)

  ## ---- ##
  ## User ##
  ## ---- ##

  def user_insert(self, user):
    json = user.json(private = True)
    json['_id'] = json['name']
    try:
      self.__couchdb['users'].save(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.User.Duplicate()

  def user_fetch(self, name):
    try:
      json = self.__couchdb['users'][name]
      json = dict(json)
      json['id'] = json['_id']
      del json['_id']
      return json
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.User.NotFound()


  def user_update(self, id, diff = None):
    args = {
      name: json.dumps(value)
      for name, value in diff.items()
      if value is not None
    }
    try:
      self.__couchdb['users'].update_doc(
        'beyond/update',
        id,
        **args
      )
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.User.NotFound()

  def __user_per_name(user):
    yield user['name'], user

  def __user_update(user, req):
    if user is None:
      return [
        None,
        {
          'code': 404,
        }
      ]
    import json
    update = {
      name: json.loads(value)
      for name, value in req['query'].items()
    }
    for id, account in update.get('dropbox_accounts', {}).items():
      user.setdefault('dropbox_accounts', {})[id] = account
    for id, account in update.get('google_accounts', {}).items():
      user.setdefault('google_accounts', {})[id] = account
    return [user, {'json': json.dumps(user)}]

  def __network_update(network, req):
    if network is None:
      return [
        None,
        {
          'code': 404,
        }
      ]
    import json
    update = {
      name: json.loads(value)
      for name, value in req['query'].items()
    }
    for user, passport in update.get('passports', {}).items():
      network.setdefault('passports', {})[user] = passport
    for user, node in update.get('endpoints', {}).items():
      for node, endpoints in node.items():
        n = network.setdefault('endpoints', {})
        u = n.setdefault(user, {})
        if endpoints is None:
          u.pop(node, None)
          if not u:
            n.pop(user)
        else:
          u[node] = endpoints
    return [network, {'json': json.dumps(update)}]

  def network_update(self, id, diff):
    args = {
      name: json.dumps(value)
      for name, value in diff.items()
      if value is not None
    }
    try:
      self.__couchdb['networks'].update_doc(
        'beyond/update',
        id,
        **args
      )
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.Network.NotFound()

  ## ------- ##
  ## Network ##
  ## ------- ##

  def network_insert(self, network):
    json = network.json()
    json['_id'] = network.name
    try:
      self.__couchdb['networks'].save(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.Network.Duplicate()

  def network_fetch(self, owner, name):
    try:
      json = self.__couchdb['networks']['%s/%s' % (owner, name)]
      return infinit.beyond.Network.from_json(self.beyond, json)
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.Network.NotFound()

  ## ------ ##
  ## Volume ##
  ## ------ ##

  def volume_insert(self, volume):
    json = volume.json()
    json['_id'] = volume.name
    try:
      self.__couchdb['volumes'].save(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.Volume.Duplicate()

  def volume_fetch(self, owner, name):
    try:
      json = self.__couchdb['volumes']['%s/%s' % (owner, name)]
      return infinit.beyond.Volume.from_json(self.beyond, json)
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.Volume.NotFound()
