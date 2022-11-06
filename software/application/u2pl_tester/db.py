import boto3 as aws
import os

with open(os.path.expanduser("~/.config/aws_credentials"), "r") as cred:
    lines = [line for line in cred]
    ACCESS_KEY = lines[0].strip()
    SECRET_KEY = lines[1].strip()

dynamodb = aws.resource('dynamodb', region_name = 'us-east-1', aws_access_key_id = ACCESS_KEY, aws_secret_access_key = SECRET_KEY)
r = dynamodb.get_available_subresources()
table = dynamodb.Table('test')
print(table)
for item in table.scan()['Items']:
    print(item)

#for i in range(10):
#    table.put_item(Item = { 'boardrev': 16, 'uniqueid': f'{0x87163BD*i:012x}', 'serial': f'{i:04d}'})
